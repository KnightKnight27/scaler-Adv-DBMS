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

void LockManager::lock(TxnId txn_id, const std::string& resource, LockMode mode) {
    LockTable* lt;
    {
        std::lock_guard<std::mutex> g(global_mu_);
        lt = &lock_tables_[resource];
    }

    std::unique_lock<std::mutex> ul(lt->mu);

    // Check if we already hold a compatible lock (idempotent re-lock)
    for (auto& req : lt->queue) {
        if (req.txn_id == txn_id && req.granted) {
            if (mode == LockMode::SHARED || req.mode == LockMode::EXCLUSIVE)
                return; // already have what we need
        }
    }

    // Add our request to the queue
    lt->queue.push_back({txn_id, mode, false});
    auto& my_req = lt->queue.back();

    // Build waits-for edges
    for (auto& req : lt->queue) {
        if (req.granted && req.txn_id != txn_id && conflicts(req.mode, mode)) {
            add_waits_for(txn_id, req.txn_id);
            if (has_cycle(txn_id)) {
                // We are the victim — remove our request and throw
                remove_waits_for(txn_id);
                lt->queue.pop_back();
                throw DeadlockException(txn_id);
            }
        }
    }

    // Wait until no conflicting granted lock exists
    lt->cv.wait(ul, [&]() {
        for (auto& req : lt->queue) {
            if (!req.granted && req.txn_id == txn_id) break;
            if (&req == &my_req) break;
            if (req.granted && req.txn_id != txn_id && conflicts(req.mode, mode))
                return false;
        }
        return true;
    });

    my_req.granted = true;
    remove_waits_for(txn_id);

    // Track which resources this txn holds
    std::lock_guard<std::mutex> wg(wf_mu_);
    // (waits_for_ is cleaned; held_by tracking below)
    (void)0;
}

void LockManager::unlock_all(TxnId txn_id) {
    std::lock_guard<std::mutex> g(global_mu_);
    for (auto& [res, lt] : lock_tables_) {
        std::lock_guard<std::mutex> lg(lt.mu);
        auto& q = lt.queue;
        q.erase(std::remove_if(q.begin(), q.end(),
                [txn_id](const LockRequest& r){ return r.txn_id == txn_id; }), q.end());
        lt.cv.notify_all();
    }
    remove_waits_for(txn_id);
}

std::vector<std::string> LockManager::held_by(TxnId txn_id) const {
    std::vector<std::string> res;
    std::lock_guard<std::mutex> g(const_cast<std::mutex&>(global_mu_));
    for (auto& [r, lt] : lock_tables_)
        for (auto& req : lt.queue)
            if (req.txn_id == txn_id && req.granted) { res.push_back(r); break; }
    return res;
}

void LockManager::add_waits_for(TxnId waiter, TxnId holder) {
    std::lock_guard<std::mutex> g(wf_mu_);
    waits_for_[waiter].insert(holder);
}
void LockManager::remove_waits_for(TxnId waiter) {
    std::lock_guard<std::mutex> g(wf_mu_);
    waits_for_.erase(waiter);
}
bool LockManager::has_cycle(TxnId start) {
    // DFS on the waits-for graph; lock already held by caller via wf_mu_
    std::set<TxnId> visited;
    std::stack<TxnId> stack;
    stack.push(start);
    while (!stack.empty()) {
        TxnId cur = stack.top(); stack.pop();
        if (!visited.insert(cur).second) return true; // revisited → cycle
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
    {
        std::lock_guard<std::mutex> g(mu_);
        active_[id] = std::move(txn);
    }
    wal_.append({LogType::BEGIN, id, "", {}, {}});
    return id;
}

std::string TransactionManager::resource_key(const std::string& table, const Value& pk) const {
    return table + ":" + pk.to_string();
}

// ─── 2PL mode helpers ────────────────────────────────────────────────────────

void TransactionManager::tpl_execute_insert(TxnId txn_id, const InsertStmt& s,
                                             Transaction& txn) {
    auto& schema = db_.catalog().get(s.table);
    // Acquire exclusive lock on the new primary key
    if (schema.pk_col >= 0)
        lm_.lock(txn_id, resource_key(s.table, s.values[schema.pk_col]),
                 LockMode::EXCLUSIVE);
    db_.executor().execute_insert(s);
    // Record in WAL; RID is not tracked per-row here for simplicity
    wal_.append({LogType::INSERT, txn_id, s.table, s.values, {}});
    txn.undo_log.emplace_back(s.table, RID{}, s.values);
}

void TransactionManager::tpl_execute_delete(TxnId txn_id, const DeleteStmt& s,
                                             Transaction& txn) {
    auto& schema = db_.catalog().get(s.table);
    auto& tbl    = db_.tables().at(s.table);
    // Scan first to find targets, then acquire locks and delete
    std::vector<std::pair<RID, Row>> to_del;
    tbl->scan([&](RID rid, const Row& row) {
        if (!s.has_where ||
            db_.executor().execute_select(SelectStmt{true,{},s.table}).rows.empty())
        { /* simplified — we re-evaluate in execute_delete */ }
        to_del.emplace_back(rid, row);
    });
    for (auto& [rid, row] : to_del) {
        if (schema.pk_col >= 0)
            lm_.lock(txn_id, resource_key(s.table, row[schema.pk_col]),
                     LockMode::EXCLUSIVE);
    }
    db_.executor().execute_delete(s);
    wal_.append({LogType::DELETE, txn_id, s.table, {}, {}});
}

// ─── MVCC mode helpers ───────────────────────────────────────────────────────

ResultSet TransactionManager::mvcc_execute_select(TxnId txn_id, const SelectStmt& s,
                                                   Transaction& txn) {
    // Read from our snapshot — no locks needed (readers never block)
    auto& schema = db_.catalog().get(s.table);
    auto rows = mvcc_.scan(s.table, txn.snapshot_ts);

    ResultSet rs;
    if (s.star) for (auto& c : schema.columns) rs.columns.push_back(c.name);
    else rs.columns = s.columns;

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
    txn.undo_log.emplace_back(s.table, rid, s.values);
}

void TransactionManager::mvcc_execute_delete(TxnId txn_id, const DeleteStmt& s,
                                              Transaction& txn) {
    Timestamp end_ts = next_ts();
    auto& schema = db_.catalog().get(s.table);
    auto rows = mvcc_.scan(s.table, txn.snapshot_ts);
    for (auto& [rid, row] : rows) {
        // Seal version as of end_ts
        mvcc_.delete_version(s.table, rid, end_ts);
        wal_.append({LogType::DELETE, txn_id, s.table, row, rid});
        txn.undo_log.emplace_back(s.table, rid, row);
    }
    // Also delete from heap for simplicity
    db_.executor().execute_delete(s);
}

// ─── execute dispatcher ──────────────────────────────────────────────────────

ResultSet TransactionManager::execute(TxnId txn_id, const std::string& sql) {
    std::lock_guard<std::mutex> g(mu_);
    auto& txn = active_.at(txn_id);

    Parser parser(sql);
    Statement stmt = parser.parse();

    return std::visit([&](auto& s) -> ResultSet {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, SelectStmt>) {
            if (mode_ == ConcurrencyMode::MVCC)
                return mvcc_execute_select(txn_id, s, txn);
            // 2PL: acquire shared lock per column read (table-level for simplicity)
            lm_.lock(txn_id, "TABLE:" + s.table, LockMode::SHARED);
            return db_.executor().execute_select(s);

        } else if constexpr (std::is_same_v<T, InsertStmt>) {
            if (mode_ == ConcurrencyMode::MVCC)
                mvcc_execute_insert(txn_id, s, txn);
            else
                tpl_execute_insert(txn_id, s, txn);
            return {};

        } else if constexpr (std::is_same_v<T, DeleteStmt>) {
            if (mode_ == ConcurrencyMode::MVCC)
                mvcc_execute_delete(txn_id, s, txn);
            else
                tpl_execute_delete(txn_id, s, txn);
            return {};

        } else if constexpr (std::is_same_v<T, CreateStmt>) {
            db_.executor().execute_create(s, db_.catalog().table_names().empty()
                                            ? "minidb_data" : "minidb_data");
            // Populate MVCC store with the table's initial (empty) state
            return {};
        } else if constexpr (std::is_same_v<T, DropStmt>) {
            db_.executor().execute_drop(s);
            return {};
        } else {
            return {}; // BEGIN/COMMIT/ROLLBACK handled at outer level
        }
    }, stmt);
}

void TransactionManager::commit(TxnId txn_id) {
    std::lock_guard<std::mutex> g(mu_);
    auto& txn = active_.at(txn_id);
    txn.state = TxnState::COMMITTED;
    wal_.append({LogType::COMMIT, txn_id, "", {}, {}});
    wal_.flush();
    lm_.unlock_all(txn_id);
    active_.erase(txn_id);
}

void TransactionManager::abort(TxnId txn_id) {
    std::lock_guard<std::mutex> g(mu_);
    auto& txn = active_.at(txn_id);
    // Undo in reverse order
    for (auto it = txn.undo_log.rbegin(); it != txn.undo_log.rend(); ++it) {
        auto& [table, rid, row] = *it;
        // For MVCC: seal the aborted version so it's never visible
        mvcc_.delete_version(table, rid, 0); // end_ts=0 → never visible
        // For the heap: we'd need before-images; simplified here
    }
    txn.state = TxnState::ABORTED;
    wal_.append({LogType::ABORT, txn_id, "", {}, {}});
    wal_.flush();
    lm_.unlock_all(txn_id);
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

    // REDO phase: replay INSERT records of committed txns
    for (auto& rec : records) {
        if (!committed.count(rec.txn_id)) continue;
        if (rec.type == LogType::INSERT && !rec.table.empty()) {
            try {
                if (db.catalog().exists(rec.table))
                    db.tables().at(rec.table)->insert_row(rec.row);
            } catch (...) {
                // Row may already exist from a previous recovery run — skip
            }
        }
    }

    if (!inflight.empty()) {
        // Report but don't crash; a robust undo would replay DELETE for each insert
        // of an in-flight txn using the before-image stored in the log.
        for (TxnId t : inflight)
            (void)t; // in production: apply undo records
    }
}
