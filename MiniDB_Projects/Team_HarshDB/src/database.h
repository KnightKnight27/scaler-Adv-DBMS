// ---------------------------------------------------------------------------
// database.h - the top-level MiniDB object that owns every subsystem.
//
// It wires the disk manager, buffer pool, WAL, catalog, transaction manager,
// lock manager, optimizer and executor together, runs crash recovery at startup,
// and exposes a small API: begin / commit / abort transactions and run() a SQL
// statement inside one. This is the single object the REPL and the benchmarks
// talk to.
// ---------------------------------------------------------------------------
#pragma once
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "catalog/catalog.h"
#include "txn/transaction.h"
#include "txn/lock_manager.h"
#include "recovery/wal.h"
#include "optimizer/optimizer.h"
#include "execution/executor.h"
#include "sql/parser.h"
#include <memory>
#include <unordered_set>

namespace minidb {

struct ExecResult {
    bool        is_query = false;
    ResultSet   rs;
    std::string message;
};

class Database {
public:
    explicit Database(const std::string& path, bool mvcc_mode = true, int buffer_pages = 256) {
        dm_   = std::make_unique<DiskManager>(path + ".db");
        bp_   = std::make_unique<BufferPool>(dm_.get(), buffer_pages);
        wal_  = std::make_unique<WAL>(path + ".wal");
        tm_   = std::make_unique<TransactionManager>();
        lm_   = std::make_unique<LockManager>();
        cat_  = std::make_unique<Catalog>(bp_.get(), path + ".catalog");
        opt_  = std::make_unique<Optimizer>(cat_.get());
        exec_ = std::make_unique<Executor>(cat_.get(), tm_.get(), lm_.get(),
                                           wal_.get(), opt_.get(), mvcc_mode);
        cat_->load();
        recover();
    }

    void set_mvcc(bool on) { exec_->set_mvcc(on); }

    // ---- transaction control ----
    TxId begin() { TxId x = tm_->begin(); wal_->log_begin(x); return x; }

    void commit(TxId xid) {
        tm_->set_committed(xid);
        wal_->log_commit(xid);   // durability point (forced to disk)
        lm_->release_all(xid);   // shrinking phase of Strict 2PL
    }

    void abort(TxId xid) {
        tm_->set_aborted(xid);
        wal_->log_abort(xid);
        lm_->release_all(xid);   // MVCC makes the aborted versions invisible automatically
    }

    // Run a single CRUD/DDL/SELECT statement inside transaction `xid`.
    ExecResult run(const std::string& sql, TxId xid) {
        Parser p(sql);
        Statement st = p.parse();
        ExecResult r;
        switch (st.type) {
            case StmtType::Create:
                exec_->create_table(st.create);
                r.message = "table '" + st.create.table + "' created";
                break;
            case StmtType::Insert:
                exec_->insert(st.insert, xid);
                r.message = "1 row inserted";
                break;
            case StmtType::Delete: {
                int n = exec_->remove(st.del, xid);
                r.message = std::to_string(n) + " row(s) deleted";
                break;
            }
            case StmtType::Select:
                r.is_query = true;
                r.rs = exec_->select(st.select, xid);
                break;
            default:
                throw std::runtime_error("transaction control must be issued directly");
        }
        return r;
    }

    // Convenience wrapper: run a statement in its own auto-committed transaction.
    ExecResult run_autocommit(const std::string& sql) {
        TxId x = begin();
        try {
            ExecResult r = run(sql, x);
            commit(x);
            return r;
        } catch (...) {
            abort(x);
            throw;
        }
    }

    // Flush dirty pages and persist catalog - a clean shutdown / checkpoint.
    void close() {
        bp_->flush_all();
        cat_->persist();
    }

    // Simulate a crash: drop all buffered (possibly dirty) pages without writing
    // them back, so only what reached disk + the WAL survives.
    void simulate_crash() { bp_->evict_all_without_flush(); }

    Catalog*            catalog()  { return cat_.get(); }
    TransactionManager* txn_mgr()  { return tm_.get(); }
    Optimizer*          optimizer(){ return opt_.get(); }

private:
    // REDO recovery: replay the WAL, reinstating everything committed
    // transactions did and ignoring everything uncommitted ones did.
    void recover() {
        auto recs = wal_->read_all();
        std::unordered_set<TxId> committed;
        TxId maxtid = 0;
        for (auto& r : recs) {
            maxtid = std::max(maxtid, r.txid);
            if (r.type == LogType::COMMIT) committed.insert(r.txid);
        }
        for (TxId x : committed) tm_->register_committed(x);
        tm_->ensure_next_above(maxtid);

        int redone = 0;
        for (auto& r : recs) {
            if (!committed.count(r.txid)) continue;
            if (r.type == LogType::INSERT) {
                TableInfo* t = cat_->get(r.table);
                if (!t) continue;
                if (!present_and_live(t, r.pk)) { exec_->redo_insert(r.table, r.row, r.txid); redone++; }
            } else if (r.type == LogType::DELETE) {
                TableInfo* t = cat_->get(r.table);
                if (!t || t->pk_index < 0 || !t->index) continue;
                RID rid = t->index->search(r.pk);
                if (rid.valid()) {
                    StoredTuple st;
                    if (t->heap->get(rid, st) && st.xmax == INVALID_TX)
                        t->heap->set_xmax(rid, r.txid);
                }
            }
        }
        if (redone > 0) bp_->flush_all();
    }

    bool present_and_live(TableInfo* t, int64_t pk) {
        if (t->pk_index < 0 || !t->index) return false;
        RID rid = t->index->search(pk);
        if (!rid.valid()) return false;
        StoredTuple st;
        return t->heap->get(rid, st) && st.xmax == INVALID_TX;
    }

    std::unique_ptr<DiskManager>        dm_;
    std::unique_ptr<BufferPool>         bp_;
    std::unique_ptr<WAL>                wal_;
    std::unique_ptr<TransactionManager> tm_;
    std::unique_ptr<LockManager>        lm_;
    std::unique_ptr<Catalog>            cat_;
    std::unique_ptr<Optimizer>          opt_;
    std::unique_ptr<Executor>           exec_;
};

} // namespace minidb
