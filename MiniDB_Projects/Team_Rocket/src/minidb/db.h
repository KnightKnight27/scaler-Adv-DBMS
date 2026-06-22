#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "query/executor.h"
#include "query/parser.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"
#include "types.h"

namespace minidb {

// The database. Owns the on-disk files (.data / .wal / .meta), the in-memory
// services, and the transaction currently in flight. Runs ARIES-style recovery
// (redo committed work, undo losers) on startup.
class Database {
public:
    explicit Database(const std::string& path)
        : path_(path), dm_(path + ".data"), log_(path + ".wal"), bp_(dm_, 256, &log_) {
        cat_.load(path_ + ".meta");
        recover();
    }

    ~Database() { checkpoint(); }

    Result execute(const std::string& sql) {
        Statement st = Parser::parse(sql);
        if (!st.error.empty()) return err("parse error: " + st.error);

        switch (st.kind) {
            case StmtKind::Begin: return begin();
            case StmtKind::Commit: return commit_stmt();
            case StmtKind::Abort: return abort_stmt();
            case StmtKind::Crash: return crash_stmt();
            case StmtKind::CreateTable: return create_table(st);
            case StmtKind::Insert: return run(st, true);
            case StmtKind::Delete: return run(st, true);
            case StmtKind::Select: return run(st, false);
            default: return err("unrecognised statement");
        }
    }

    // Flush dirty pages and persist the catalog: a clean shutdown point.
    void checkpoint() {
        bp_.flush_all();
        cat_.save(path_ + ".meta");
    }

private:
    std::string path_;
    DiskManager dm_;
    LogManager log_;
    BufferPool bp_;
    Catalog cat_;
    LockManager lm_;
    std::unique_ptr<Transaction> txn_;  // explicit (BEGIN) transaction, if any
    int next_txn_ = 1;

    static Result err(const std::string& m) {
        Result r;
        r.ok = false;
        r.message = m;
        return r;
    }

    void log_begin(Transaction* t) {
        LogRecord r;
        r.txn_id = t->id;
        r.type = LogType::Begin;
        log_.append(r);
    }

    Result begin() {
        if (txn_) return err("a transaction is already active");
        txn_ = std::make_unique<Transaction>();
        txn_->id = next_txn_++;
        txn_->autocommit = false;
        log_begin(txn_.get());
        return ok_msg("BEGIN (txn " + std::to_string(txn_->id) + ")");
    }

    Result commit_stmt() {
        if (!txn_) return err("no active transaction");
        int id = txn_->id;
        commit(txn_.get());
        txn_.reset();
        return ok_msg("COMMIT (txn " + std::to_string(id) + ")");
    }

    Result abort_stmt() {
        if (!txn_) return err("no active transaction");
        int id = txn_->id;
        abort(txn_.get());
        txn_.reset();
        return ok_msg("ABORT (txn " + std::to_string(id) + ")");
    }

    Result crash_stmt() {
        bp_.discard_all();  // throw away everything not yet on disk
        txn_.reset();
        recover();
        return ok_msg("simulated crash; recovered from write-ahead log");
    }

    static Result ok_msg(const std::string& m) {
        Result r;
        r.message = m;
        return r;
    }

    Result create_table(const Statement& st) {
        if (cat_.exists(st.table)) return err("table already exists: " + st.table);
        cat_.create(st.table, st.schema);
        cat_.save(path_ + ".meta");
        return ok_msg("table " + st.table + " created");
    }

    // Run a statement inside a transaction: the explicit one if a BEGIN is
    // active, otherwise an implicit single-statement transaction.
    Result run(const Statement& st, bool writes) {
        if (st.kind == StmtKind::Select) {
            if (!cat_.exists(st.table)) return err("no such table: " + st.table);
            if (st.has_join && !cat_.exists(st.join_table))
                return err("no such table: " + st.join_table);
        }

        bool implicit = !txn_;
        std::unique_ptr<Transaction> local;
        Transaction* t;
        if (implicit) {
            local = std::make_unique<Transaction>();
            local->id = next_txn_++;
            local->autocommit = true;
            t = local.get();
            if (writes) log_begin(t);
        } else {
            t = txn_.get();
        }

        ExecContext ctx{&cat_, &bp_, &lm_, &log_, t};
        try {
            Result r;
            if (st.kind == StmtKind::Insert)
                r = do_insert(ctx, st);
            else if (st.kind == StmtKind::Delete)
                r = do_delete(ctx, st);
            else
                r = Executor::run_select(&ctx, st);
            if (implicit) {
                if (writes)
                    commit(t);
                else
                    lm_.release_all(t->id);  // read-only: just drop locks, nothing logged
            }
            return r;
        } catch (const TxnAbort& e) {
            abort(t);
            if (!implicit) txn_.reset();
            return err(e.message);
        } catch (const std::exception& e) {
            abort(t);
            if (!implicit) txn_.reset();
            return err(std::string("error: ") + e.what());
        }
    }

    Result do_insert(ExecContext& ctx, const Statement& st) {
        if (!cat_.exists(st.table)) return err("no such table: " + st.table);
        TableInfo& ti = cat_.get(st.table);
        if (st.values.size() != ti.schema.size()) return err("column count mismatch");

        Tuple tup;
        for (size_t i = 0; i < ti.schema.size(); ++i) tup.push_back(coerce(st.values[i], ti.schema[i].type));
        std::vector<uint8_t> bytes = serialize_tuple(ti.schema, tup);

        HeapFile heap(bp_, ti.page_ids);
        RID rid = heap.insert(bytes);
        ctx.lock_or_abort(st.table, rid, LockMode::Exclusive);

        LogRecord rec;
        rec.txn_id = ctx.txn->id;
        rec.type = LogType::Insert;
        rec.table = st.table;
        rec.rid = rid;
        rec.image = bytes;
        int64_t lsn = log_.append(rec);
        heap.set_lsn(rid.page_id, lsn);

        if (ti.index_col >= 0) ti.index->insert(tup[ti.index_col].i, rid);
        ++ti.num_tuples;
        return ok_msg("1 row inserted");
    }

    Result do_delete(ExecContext& ctx, const Statement& st) {
        if (!cat_.exists(st.table)) return err("no such table: " + st.table);
        TableInfo& ti = cat_.get(st.table);
        HeapFile heap(bp_, ti.page_ids);

        struct Victim {
            RID rid;
            std::vector<uint8_t> image;
        };
        std::vector<Victim> victims;
        heap.scan([&](RID rid, const std::vector<uint8_t>& b) {
            Tuple t = deserialize_tuple(ti.schema, b.data(), static_cast<int>(b.size()));
            if (!st.where.present || eval_predicate(t, ti.schema, st.where))
                victims.push_back({rid, b});
        });

        for (const Victim& v : victims) {
            ctx.lock_or_abort(st.table, v.rid, LockMode::Exclusive);
            LogRecord rec;
            rec.txn_id = ctx.txn->id;
            rec.type = LogType::Delete;
            rec.table = st.table;
            rec.rid = v.rid;
            rec.image = v.image;  // before-image, needed to undo
            int64_t lsn = log_.append(rec);
            heap.mark_delete(v.rid);
            heap.set_lsn(v.rid.page_id, lsn);
            if (ti.index_col >= 0) {
                Tuple t = deserialize_tuple(ti.schema, v.image.data(), static_cast<int>(v.image.size()));
                ti.index->erase(t[ti.index_col].i);
            }
            --ti.num_tuples;
        }
        return ok_msg(std::to_string(victims.size()) + " row(s) deleted");
    }

    void commit(Transaction* t) {
        LogRecord r;
        r.txn_id = t->id;
        r.type = LogType::Commit;
        log_.append(r);
        log_.flush();  // commit is durable once its log record is on disk
        lm_.release_all(t->id);
        cat_.save(path_ + ".meta");
        t->state = TxnState::Committed;
    }

    void abort(Transaction* t) {
        const std::vector<LogRecord>& recs = log_.records();
        for (int i = static_cast<int>(recs.size()) - 1; i >= 0; --i) {
            const LogRecord& r = recs[i];
            if (r.txn_id != t->id) continue;
            if (r.type == LogType::Begin) break;
            apply_undo(r);
        }
        LogRecord a;
        a.txn_id = t->id;
        a.type = LogType::Abort;
        log_.append(a);
        log_.flush();
        lm_.release_all(t->id);
        t->state = TxnState::Aborted;
    }

    void apply_undo(const LogRecord& r) {
        if (!cat_.exists(r.table)) return;
        TableInfo& ti = cat_.get(r.table);
        HeapFile heap(bp_, ti.page_ids);
        if (r.type == LogType::Insert) {
            heap.mark_delete(r.rid);
            if (ti.index_col >= 0) {
                Tuple t = deserialize_tuple(ti.schema, r.image.data(), static_cast<int>(r.image.size()));
                ti.index->erase(t[ti.index_col].i);
            }
            --ti.num_tuples;
        } else if (r.type == LogType::Delete) {
            heap.put_at(r.rid, r.image);
            if (ti.index_col >= 0) {
                Tuple t = deserialize_tuple(ti.schema, r.image.data(), static_cast<int>(r.image.size()));
                ti.index->insert(t[ti.index_col].i, r.rid);
            }
            ++ti.num_tuples;
        }
    }

    // ARIES recovery: redo the entire history, then undo transactions that
    // never committed. Indexes and row counts are rebuilt from the base data.
    void recover() {
        const std::vector<LogRecord>& recs = log_.records();
        std::set<int> committed;
        for (const LogRecord& r : recs)
            if (r.type == LogType::Commit) committed.insert(r.txn_id);

        for (const LogRecord& r : recs) redo(r);
        for (int i = static_cast<int>(recs.size()) - 1; i >= 0; --i) {
            const LogRecord& r = recs[i];
            if (committed.count(r.txn_id)) continue;
            undo_loser(r);
        }

        bp_.flush_all();
        dm_.recompute_num_pages();
        rebuild_indexes();
    }

    void redo(const LogRecord& r) {
        if (!cat_.exists(r.table)) return;
        TableInfo& ti = cat_.get(r.table);
        HeapFile heap(bp_, ti.page_ids);
        if (r.type == LogType::Insert)
            heap.put_at(r.rid, r.image);
        else if (r.type == LogType::Delete)
            heap.mark_delete(r.rid);
    }

    void undo_loser(const LogRecord& r) {
        if (!cat_.exists(r.table)) return;
        TableInfo& ti = cat_.get(r.table);
        HeapFile heap(bp_, ti.page_ids);
        if (r.type == LogType::Insert)
            heap.mark_delete(r.rid);
        else if (r.type == LogType::Delete)
            heap.put_at(r.rid, r.image);
    }

    void rebuild_indexes() {
        for (const auto& kv : cat_.all()) {
            TableInfo& ti = *kv.second;
            if (ti.index_col >= 0) ti.index = std::make_unique<BPlusTree>();
            int64_t count = 0;
            HeapFile heap(bp_, ti.page_ids);
            heap.scan([&](RID rid, const std::vector<uint8_t>& b) {
                ++count;
                if (ti.index_col >= 0) {
                    Tuple t = deserialize_tuple(ti.schema, b.data(), static_cast<int>(b.size()));
                    ti.index->insert(t[ti.index_col].i, rid);
                }
            });
            ti.num_tuples = count;
        }
    }

    static Value coerce(const Value& v, Type t) {
        if (t == Type::Int)
            return v.type == Type::Int ? v : Value::make_int(std::strtoll(v.s.c_str(), nullptr, 10));
        return v.type == Type::Text ? v : Value::make_text(v.to_string());
    }
};

}  // namespace minidb
