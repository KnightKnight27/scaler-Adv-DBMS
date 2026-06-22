#include "transaction.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <stack>

// ─── WAL ─────────────────────────────────────────────────────────────────────
//
// Binary record format:
//   [1B LogType][8B TxnId][2B table_len][table_len bytes]
//   [2B col_count]
//   per column: [1B type][1B is_null][8B int | 4B+N varchar]
//   [4B page_id][2B slot_id]
// ─────────────────────────────────────────────────────────────────────────────

static void write_u8(std::ostream& os, uint8_t v)   { os.write((char*)&v, 1); }
static void write_u16(std::ostream& os, uint16_t v)  { os.write((char*)&v, 2); }
static void write_u32(std::ostream& os, uint32_t v)  { os.write((char*)&v, 4); }
static void write_u64(std::ostream& os, uint64_t v)  { os.write((char*)&v, 8); }

static uint8_t  read_u8(std::istream& is)  { uint8_t v;  is.read((char*)&v,1); return v; }
static uint16_t read_u16(std::istream& is) { uint16_t v; is.read((char*)&v,2); return v; }
static uint32_t read_u32(std::istream& is) { uint32_t v; is.read((char*)&v,4); return v; }
static uint64_t read_u64(std::istream& is) { uint64_t v; is.read((char*)&v,8); return v; }

static void write_value(std::ostream& os, const Value& v) {
    write_u8(os, (uint8_t)v.type);
    write_u8(os, v.is_null ? 1 : 0);
    if (!v.is_null) {
        if (v.type == Type::INT) {
            int64_t x = v.as_int();
            os.write((char*)&x, 8);
        } else {
            const std::string& s = v.as_str();
            write_u32(os, (uint32_t)s.size());
            os.write(s.data(), s.size());
        }
    }
}
static Value read_value(std::istream& is) {
    Type type = (Type)read_u8(is);
    bool is_null = read_u8(is) != 0;
    if (is_null) return Value::make_null(type);
    if (type == Type::INT) {
        int64_t x; is.read((char*)&x, 8); return Value::make_int(x);
    } else {
        uint32_t len = read_u32(is);
        std::string s(len, '\0');
        is.read(s.data(), len);
        return Value::make_varchar(std::move(s));
    }
}

WAL::WAL(const std::string& path) : path_(path) {
    out_.open(path, std::ios::app | std::ios::binary);
}
WAL::~WAL() { if (out_.is_open()) { out_.flush(); out_.close(); } }

void WAL::append(const LogRecord& rec) {
    write_u8(out_,  (uint8_t)rec.type);
    write_u64(out_, rec.txn_id);
    write_u16(out_, (uint16_t)rec.table.size());
    out_.write(rec.table.data(), rec.table.size());
    write_u16(out_, (uint16_t)rec.row.size());
    for (auto& v : rec.row) write_value(out_, v);
    write_u32(out_, rec.rid.page_id);
    write_u16(out_, rec.rid.slot_id);
    out_.flush();
}

void WAL::flush() { out_.flush(); }

std::vector<LogRecord> WAL::read_all() const {
    std::vector<LogRecord> records;
    std::ifstream in(path_, std::ios::binary);
    while (in && in.peek() != EOF) {
        LogRecord rec;
        rec.type   = (LogType)read_u8(in);
        rec.txn_id = read_u64(in);
        uint16_t tlen = read_u16(in);
        rec.table.resize(tlen);
        in.read(rec.table.data(), tlen);
        uint16_t ncols = read_u16(in);
        for (int i = 0; i < ncols; ++i) rec.row.push_back(read_value(in));
        rec.rid.page_id = read_u32(in);
        rec.rid.slot_id = read_u16(in);
        if (in) records.push_back(std::move(rec));
    }
    return records;
}

// ─── LockManager ─────────────────────────────────────────────────────────────

bool LockManager::conflicts(LockMode held, LockMode requested) const {
    // Shared-Shared is compatible; everything else conflicts
    return !(held == LockMode::SHARED && requested == LockMode::SHARED);
}

// Is requested mode compatible with ALL locks currently held on this resource
// by OTHER transactions? `q` holds only granted locks (see note in lock()).
static bool compatible_with_holders(const std::vector<LockRequest>& q,
                                    TxnId txn_id, LockMode mode) {
    for (const auto& r : q) {
        if (r.txn_id == txn_id) continue;               // our own lock never conflicts
        if (!(r.mode == LockMode::SHARED && mode == LockMode::SHARED))
            return false;                               // S/S ok; anything with X conflicts
    }
    return true;
}

void LockManager::lock(TxnId txn_id, const std::string& resource, LockMode mode) {
    LockTable* lt;
    {
        std::lock_guard<std::mutex> g(global_mu_);
        lt = &lock_tables_[resource];
    }

    std::unique_lock<std::mutex> ul(lt->mu);

    // `lt->queue` holds ONLY currently-granted locks. We never store an
    // ungranted request in it, so there are no dangling references across the
    // wait below (the earlier bug: a reference into the vector was invalidated
    // when another thread push_back'd while we waited).

    // Idempotent re-lock: already hold something strong enough?
    for (const auto& req : lt->queue)
        if (req.txn_id == txn_id &&
            (mode == LockMode::SHARED || req.mode == LockMode::EXCLUSIVE))
            return;

    // Collect every conflicting current holder, then atomically register the
    // waits-for edges and check for a cycle. If a cycle exists, we are the victim.
    std::vector<TxnId> holders;
    for (const auto& req : lt->queue)
        if (req.txn_id != txn_id &&
            !(req.mode == LockMode::SHARED && mode == LockMode::SHARED))
            holders.push_back(req.txn_id);

    if (!holders.empty() && register_and_detect(txn_id, holders)) {
        remove_waits_for(txn_id);
        throw DeadlockException(txn_id);
    }

    // Writer preference: while a writer is waiting, new SHARED requests yield to
    // it. Without this, a steady stream of readers can starve the writer forever
    // (it never sees a moment with zero shared holders).
    if (mode == LockMode::EXCLUSIVE) lt->waiting_exclusive++;

    // Block until compatible with all holders. The predicate re-reads the queue
    // each wake-up, so it is robust to other threads mutating it meanwhile.
    lt->cv.wait(ul, [&]() {
        if (!compatible_with_holders(lt->queue, txn_id, mode)) return false;
        if (mode == LockMode::SHARED && lt->waiting_exclusive > 0) return false;
        return true;
    });

    if (mode == LockMode::EXCLUSIVE) lt->waiting_exclusive--;
    remove_waits_for(txn_id);
    lt->queue.push_back({txn_id, mode, true}); // record the granted lock
}

void LockManager::unlock_all(TxnId txn_id) {
    std::lock_guard<std::mutex> g(global_mu_);
    for (auto& [res, lt] : lock_tables_) {
        {
            std::lock_guard<std::mutex> lg(lt.mu);
            auto& q = lt.queue;
            q.erase(std::remove_if(q.begin(), q.end(),
                    [txn_id](const LockRequest& r){ return r.txn_id == txn_id; }), q.end());
        }
        lt.cv.notify_all(); // wake any waiters so they can re-check compatibility
    }
    remove_waits_for(txn_id);
}

std::vector<std::string> LockManager::held_by(TxnId txn_id) const {
    std::vector<std::string> res;
    std::lock_guard<std::mutex> g(const_cast<std::mutex&>(global_mu_));
    for (auto& [r, lt] : lock_tables_)
        for (auto& req : lt.queue)
            if (req.txn_id == txn_id) { res.push_back(r); break; }
    return res;
}

void LockManager::remove_waits_for(TxnId waiter) {
    std::lock_guard<std::mutex> g(wf_mu_);
    waits_for_.erase(waiter);
}

bool LockManager::register_and_detect(TxnId waiter, const std::vector<TxnId>& holders) {
    std::lock_guard<std::mutex> g(wf_mu_);            // register + detect atomically
    for (TxnId h : holders) waits_for_[waiter].insert(h);
    return has_cycle_locked(waiter);
}

bool LockManager::has_cycle_locked(TxnId start) {
    // DFS over the waits-for graph; caller must hold wf_mu_.
    std::set<TxnId> visited;
    std::stack<TxnId> stack;
    for (TxnId next : waits_for_[start]) stack.push(next); // start from successors
    while (!stack.empty()) {
        TxnId cur = stack.top(); stack.pop();
        if (cur == start) return true;                // path returns to start → cycle
        if (!visited.insert(cur).second) continue;
        auto it = waits_for_.find(cur);
        if (it != waits_for_.end())
            for (TxnId next : it->second) stack.push(next);
    }
    return false;
}

// ─── MvccStore ────────────────────────────────────────────────────────────────

void MvccStore::add_rid_for_table(const std::string& table, RID rid) {
    std::lock_guard<std::mutex> g(mu_);
    auto& v = rids_[table];
    if (std::find(v.begin(), v.end(), rid) == v.end())
        v.push_back(rid);
}

void MvccStore::put_version(const std::string& table, RID rid,
                             const Row& new_data, Timestamp begin_ts, TxnId creator) {
    std::lock_guard<std::mutex> g(mu_);
    auto& chain = chains_[table][rid];
    // Seal the previous current version
    if (!chain.empty() && chain.back().end_ts == UINT64_MAX)
        chain.back().end_ts = begin_ts;
    chain.push_back({new_data, begin_ts, UINT64_MAX, creator});

    // Track rid
    auto& v = rids_[table];
    if (std::find(v.begin(), v.end(), rid) == v.end())
        v.push_back(rid);
}

void MvccStore::delete_version(const std::string& table, RID rid, Timestamp end_ts) {
    std::lock_guard<std::mutex> g(mu_);
    auto& chain = chains_[table][rid];
    if (!chain.empty() && chain.back().end_ts == UINT64_MAX)
        chain.back().end_ts = end_ts;
}

std::optional<Row> MvccStore::read(const std::string& table, RID rid,
                                    Timestamp snapshot_ts) const {
    std::lock_guard<std::mutex> g(mu_);
    auto ti = chains_.find(table);
    if (ti == chains_.end()) return std::nullopt;
    auto ri = ti->second.find(rid);
    if (ri == ti->second.end()) return std::nullopt;
    for (auto it = ri->second.rbegin(); it != ri->second.rend(); ++it) {
        if (it->begin_ts <= snapshot_ts && snapshot_ts < it->end_ts)
            return it->data;
    }
    return std::nullopt;
}

std::vector<std::pair<RID, Row>> MvccStore::scan(const std::string& table,
                                                   Timestamp snapshot_ts) const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::pair<RID, Row>> result;
    auto ri = rids_.find(table);
    if (ri == rids_.end()) return result;
    auto ti = chains_.find(table);
    if (ti == chains_.end()) return result;
    for (RID rid : ri->second) {
        auto ci = ti->second.find(rid);
        if (ci == ti->second.end()) continue;
        for (auto it = ci->second.rbegin(); it != ci->second.rend(); ++it) {
            if (it->begin_ts <= snapshot_ts && snapshot_ts < it->end_ts) {
                result.emplace_back(rid, it->data);
                break;
            }
        }
    }
    return result;
}

// ─── TransactionManager ───────────────────────────────────────────────────────

TransactionManager::TransactionManager(Database& db, const std::string& wal_path,
                                        ConcurrencyMode mode)
    : db_(db), wal_(wal_path), mode_(mode) {}

TxnId TransactionManager::begin() {
    TxnId id = next_txn_id_.fetch_add(1);
    Transaction txn;
    txn.id          = id;
    txn.snapshot_ts = next_ts(); // MVCC snapshot timestamp
    txn.state       = TxnState::ACTIVE;
    std::lock_guard<std::mutex> g(mu_);
    active_[id] = std::move(txn);
    // NB: we do NOT log BEGIN here. A read-only transaction writes nothing to
    // the WAL at all — BEGIN is logged lazily on the first write (see below).
    // This keeps read transactions free of disk I/O, which is what lets MVCC
    // readers run concurrently without serialising on the log.
    return id;
}

std::string TransactionManager::resource_key(const std::string& table, const Value& pk) const {
    return table + ":" + pk.to_string();
}

// ─── 2PL mode helpers ────────────────────────────────────────────────────────

// Lazily log BEGIN the first time a transaction actually modifies data, and
// mark it as a writer. Must be called while holding storage_mu_ (guards wal_).
void TransactionManager::log_begin_if_needed(Transaction& txn) {
    if (!txn.wrote) {
        wal_.append({LogType::BEGIN, txn.id, "", {}, {}});
        txn.wrote = true;
    }
}

// The 2PL table-level EXCLUSIVE lock is acquired by the dispatcher BEFORE these
// helpers run; here we only touch storage (heap + WAL), already under storage_mu_.
void TransactionManager::tpl_execute_insert(TxnId txn_id, const InsertStmt& s,
                                             Transaction& txn) {
    log_begin_if_needed(txn);
    RID rid = db_.executor().execute_insert(s);
    wal_.append({LogType::INSERT, txn_id, s.table, s.values, rid});
    // To undo an insert we delete the row again (op = INSERT).
    txn.undo_log.push_back({LogType::INSERT, s.table, rid, s.values});
}

void TransactionManager::tpl_execute_delete(TxnId txn_id, const DeleteStmt& s,
                                             Transaction& txn) {
    log_begin_if_needed(txn);
    // execute_delete returns the rows it actually removed (after WHERE filtering),
    // so we can re-insert them on abort. Record one undo entry per deleted row.
    auto deleted = db_.executor().execute_delete(s);
    for (auto& [rid, row] : deleted) {
        wal_.append({LogType::DELETE, txn_id, s.table, row, rid});
        txn.undo_log.push_back({LogType::DELETE, s.table, rid, row});
    }
}

// ─── MVCC mode helpers ───────────────────────────────────────────────────────

ResultSet TransactionManager::mvcc_execute_select(TxnId txn_id, const SelectStmt& s,
                                                   Transaction& txn) {
    // Read from our snapshot — no locks needed (readers never block).
    auto& schema = db_.catalog().get(s.table);

    ResultSet rs;
    if (s.star) for (auto& c : schema.columns) rs.columns.push_back(c.name);
    else rs.columns = s.columns;

    // Fast path: a point query on the primary key uses the B+ index to find the
    // single RID, then reads that RID's version visible to our snapshot. This
    // mirrors the 2PL path's INDEX_POINT plan so the two modes do comparable
    // work per read — otherwise MVCC would do an O(rows) version scan here.
    if (s.has_where && !s.where_cond.rhs_is_col &&
        s.where_cond.op == TokenType::EQ &&
        schema.col_index(s.where_cond.left_col) == schema.pk_col &&
        schema.pk_col >= 0) {
        auto& tbl = db_.tables().at(s.table);
        auto rid  = tbl->index().search(s.where_cond.rhs_val);
        if (rid) {
            auto row = mvcc_.read(s.table, *rid, txn.snapshot_ts);
            if (row) {
                Row out;
                if (s.star) out = *row;
                else for (auto& cn : s.columns) {
                    int i = schema.col_index(cn);
                    if (i < 0) throw std::runtime_error("Column not found: " + cn);
                    out.push_back((*row)[i]);
                }
                rs.rows.push_back(std::move(out));
            }
        }
        return rs;
    }

    // General path: scan all versions visible to the snapshot, then filter.
    auto rows = mvcc_.scan(s.table, txn.snapshot_ts);
    for (auto& [rid, row] : rows) {
        // Apply WHERE filter
        if (s.has_where) {
            // Simple column-vs-literal evaluation
            int ci = schema.col_index(s.where_cond.left_col);
            if (ci < 0) continue;
            Value lhs = row[ci];
            Value rhs = s.where_cond.rhs_is_col ? Value::make_int(0) : s.where_cond.rhs_val;
            bool pass = false;
            switch (s.where_cond.op) {
                case TokenType::EQ:  pass = (lhs == rhs); break;
                case TokenType::NEQ: pass = (lhs != rhs); break;
                case TokenType::LT:  pass = (lhs <  rhs); break;
                case TokenType::LTE: pass = (lhs <= rhs); break;
                case TokenType::GT:  pass = (lhs >  rhs); break;
                case TokenType::GTE: pass = (lhs >= rhs); break;
                default: pass = false;
            }
            if (!pass) continue;
        }
        Row out;
        if (s.star) { out = row; }
        else {
            for (auto& cn : s.columns) {
                int i = schema.col_index(cn);
                if (i < 0) throw std::runtime_error("Column not found: " + cn);
                out.push_back(row[i]);
            }
        }
        rs.rows.push_back(std::move(out));
    }
    return rs;
}

void TransactionManager::mvcc_execute_insert(TxnId txn_id, const InsertStmt& s,
                                              Transaction& txn) {
    log_begin_if_needed(txn);
    // Write to the heap table (physical storage)
    db_.executor().execute_insert(s);
    // Find the RID of the row we just inserted
    auto& schema = db_.catalog().get(s.table);
    auto& tbl    = db_.tables().at(s.table);
    RID rid{};
    if (schema.pk_col >= 0) {
        auto r = tbl->index().search(s.values[schema.pk_col]);
        if (r) rid = *r;
    }
    // Create a version visible from now on (commit_ts assigned at commit)
    Timestamp write_ts = next_ts();
    mvcc_.put_version(s.table, rid, s.values, write_ts, txn_id);
    wal_.append({LogType::INSERT, txn_id, s.table, s.values, rid});
    txn.undo_log.push_back({LogType::INSERT, s.table, rid, s.values});
}

void TransactionManager::mvcc_execute_delete(TxnId txn_id, const DeleteStmt& s,
                                              Transaction& txn) {
    log_begin_if_needed(txn);
    Timestamp end_ts = next_ts();
    auto& schema = db_.catalog().get(s.table);
    auto rows = mvcc_.scan(s.table, txn.snapshot_ts);
    for (auto& [rid, row] : rows) {
        // Seal version as of end_ts
        mvcc_.delete_version(s.table, rid, end_ts);
        wal_.append({LogType::DELETE, txn_id, s.table, row, rid});
        txn.undo_log.push_back({LogType::DELETE, s.table, rid, row});
    }
    // Also delete from heap for simplicity
    db_.executor().execute_delete(s);
}

// ─── execute dispatcher ──────────────────────────────────────────────────────

ResultSet TransactionManager::execute(TxnId txn_id, const std::string& sql) {
    // Look up the transaction under mu_ briefly, then release it. The map node
    // is stable (only this thread erases its own txn at commit/abort), so the
    // pointer stays valid while we do the lock wait and data work below.
    Transaction* txn;
    {
        std::lock_guard<std::mutex> g(mu_);
        txn = &active_.at(txn_id);
    }

    Parser parser(sql);
    Statement stmt = parser.parse();

    return std::visit([&](auto& s) -> ResultSet {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, SelectStmt>) {
            if (mode_ == ConcurrencyMode::MVCC)
                // Snapshot read: no 2PL lock, no storage_mu_. Concurrent readers
                // only briefly touch the version store's own mutex.
                return mvcc_execute_select(txn_id, s, *txn);
            // 2PL: SHARED table lock, held until commit. This is what makes a
            // concurrent writer (EXCLUSIVE) block readers — the contention MVCC avoids.
            lm_.lock(txn_id, "TABLE:" + s.table, LockMode::SHARED);
            std::lock_guard<std::mutex> sg(storage_mu_);
            return db_.executor().execute_select(s);

        } else if constexpr (std::is_same_v<T, InsertStmt>) {
            if (mode_ != ConcurrencyMode::MVCC)
                lm_.lock(txn_id, "TABLE:" + s.table, LockMode::EXCLUSIVE);
            std::lock_guard<std::mutex> sg(storage_mu_);
            if (mode_ == ConcurrencyMode::MVCC) mvcc_execute_insert(txn_id, s, *txn);
            else                                tpl_execute_insert(txn_id, s, *txn);
            return {};

        } else if constexpr (std::is_same_v<T, DeleteStmt>) {
            if (mode_ != ConcurrencyMode::MVCC)
                lm_.lock(txn_id, "TABLE:" + s.table, LockMode::EXCLUSIVE);
            std::lock_guard<std::mutex> sg(storage_mu_);
            if (mode_ == ConcurrencyMode::MVCC) mvcc_execute_delete(txn_id, s, *txn);
            else                                tpl_execute_delete(txn_id, s, *txn);
            return {};

        } else if constexpr (std::is_same_v<T, CreateStmt>) {
            std::lock_guard<std::mutex> sg(storage_mu_);
            db_.executor().execute_create(s, db_.db_dir());
            // Persist the schema so the table is found again after a restart.
            // (DDL is auto-committed: it takes effect immediately, not at COMMIT.)
            db_.catalog().save(db_.db_dir() + "/catalog.cat");
            return {};
        } else if constexpr (std::is_same_v<T, DropStmt>) {
            std::lock_guard<std::mutex> sg(storage_mu_);
            db_.executor().execute_drop(s);
            db_.catalog().save(db_.db_dir() + "/catalog.cat");
            return {};
        } else {
            return {}; // BEGIN/COMMIT/ROLLBACK handled at outer level
        }
    }, stmt);
}

void TransactionManager::commit(TxnId txn_id) {
    // Snapshot whether this txn wrote anything (under mu_), without holding mu_
    // during the WAL flush.
    bool wrote;
    {
        std::lock_guard<std::mutex> g(mu_);
        wrote = active_.at(txn_id).wrote;
        active_.at(txn_id).state = TxnState::COMMITTED;
    }
    // Only writers touch the WAL. Read-only txns commit with zero I/O.
    if (wrote) {
        std::lock_guard<std::mutex> sg(storage_mu_);
        wal_.append({LogType::COMMIT, txn_id, "", {}, {}});
        wal_.flush();
    }
    lm_.unlock_all(txn_id);                 // strict 2PL: release locks at commit
    std::lock_guard<std::mutex> g(mu_);
    active_.erase(txn_id);
}

void TransactionManager::abort(TxnId txn_id) {
    bool wrote;
    {
        std::lock_guard<std::mutex> g(mu_);
        wrote = active_.at(txn_id).wrote;
    }

    if (wrote) {
        // Undo work touches storage (heap + WAL) — do it under storage_mu_, NOT mu_.
        std::lock_guard<std::mutex> sg(storage_mu_);
        Transaction& txn = active_.at(txn_id); // safe: only this thread erases it

        // Apply undo entries in REVERSE order to restore the pre-txn state.
        for (auto it = txn.undo_log.rbegin(); it != txn.undo_log.rend(); ++it) {
            const UndoEntry& u = *it;

            if (mode_ == ConcurrencyMode::MVCC) {
                // MVCC reads consult the version store, so it is enough to make the
                // aborted version invisible: seal it with end_ts = 0 (pre-snapshot).
                mvcc_.delete_version(u.table, u.rid, 0);
                continue;
            }

            // 2PL: physically reverse the heap change.
            if (!db_.catalog().exists(u.table)) continue;
            auto& tbl    = db_.tables().at(u.table);
            auto& schema = db_.catalog().get(u.table);

            if (u.op == LogType::INSERT) {
                // Undo an insert → delete the row. Locate its current RID by PK.
                if (schema.pk_col >= 0) {
                    auto rid = tbl->index().search(u.row[schema.pk_col]);
                    if (rid) tbl->delete_row(*rid, u.row);
                }
            } else if (u.op == LogType::DELETE) {
                // Undo a delete → re-insert the before-image.
                tbl->insert_row(u.row);
            }
        }
        wal_.append({LogType::ABORT, txn_id, "", {}, {}});
        wal_.flush();
    }

    lm_.unlock_all(txn_id);
    std::lock_guard<std::mutex> g(mu_);
    active_.at(txn_id).state = TxnState::ABORTED;
    active_.erase(txn_id);
}

// ─── Crash Recovery ──────────────────────────────────────────────────────────
//
// ARIES-lite:
//  1. Read the WAL start to finish.
//  2. Track which txns committed and which were in-flight at crash.
//  3. REDO all INSERT/DELETE records of committed transactions
//     (they may not have been flushed to heap pages).
//  4. For in-flight (uncommitted) transactions: their changes may have been
//     flushed (no-force policy) so we would UNDO them. Simplified: we simply
//     report them as rolled back; a full implementation would apply before-images.
// ─────────────────────────────────────────────────────────────────────────────
void recover(Database& db, const std::string& wal_path) {
    WAL wal(wal_path);
    auto records = wal.read_all();

    std::set<TxnId>    committed, aborted, began;
    for (auto& rec : records) {
        if (rec.type == LogType::BEGIN)    began.insert(rec.txn_id);
        if (rec.type == LogType::COMMIT)   committed.insert(rec.txn_id);
        if (rec.type == LogType::ABORT)    aborted.insert(rec.txn_id);
    }

    // In-flight = began but neither committed nor aborted
    std::set<TxnId> inflight;
    for (TxnId t : began)
        if (!committed.count(t) && !aborted.count(t)) inflight.insert(t);

    // REDO phase: replay INSERT records of committed txns.
    // REDO must be IDEMPOTENT: heap pages are usually already durable (we flush
    // on commit), so the row may already exist. We skip those by checking the
    // primary-key index first, and only re-insert rows lost to an unflushed crash.
    for (auto& rec : records) {
        if (!committed.count(rec.txn_id)) continue;
        if (rec.type == LogType::INSERT && !rec.table.empty()) {
            if (!db.catalog().exists(rec.table)) continue;
            auto& tbl    = db.tables().at(rec.table);
            auto& schema = db.catalog().get(rec.table);
            // Already present? Then this insert is already durable — nothing to do.
            if (schema.pk_col >= 0 && tbl->index().search(rec.row[schema.pk_col]))
                continue;
            try { tbl->insert_row(rec.row); } catch (...) { /* defensive */ }
        }
    }

    if (!inflight.empty()) {
        // Report but don't crash; a robust undo would replay DELETE for each insert
        // of an in-flight txn using the before-image stored in the log.
        for (TxnId t : inflight)
            (void)t; // in production: apply undo records
    }
}
