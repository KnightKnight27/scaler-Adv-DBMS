#include "replication/primary.h"

namespace minidb {

Primary::Primary(HeapFile* heap, BPlusTree* tree,
                 WAL* wal, LockManager* lm, TxManager* txm)
    : heap_(heap), tree_(tree), wal_(wal), lm_(lm), txm_(txm),
      exec_(heap, tree) {}

ExecResult Primary::Execute(const std::string& sql) {
    return Execute(Parser::Parse(sql), -1);
}

ExecResult Primary::Execute(const Statement& stmt, txn_id_t active_tid) {
    auto sample_data = heap_->ScanAll();
    size_t total_records = sample_data.size();
    std::string plan = opt_.SelectPlan(stmt, total_records, sample_data);

    // Non-write statements: just execute directly.
    if (stmt.type == StmtType::SELECT || stmt.type == StmtType::SHOW || stmt.type == StmtType::SELECT_JOIN) {
        if (active_tid != -1) {
            if (stmt.type == StmtType::SELECT && stmt.id != -1) {
                if (!lm_->LockShared(active_tid, stmt.id)) {
                    ExecResult r;
                    r.success = false;
                    r.message = "ERROR: Deadlock / Lock Timeout occurred, transaction aborted.";
                    return r;
                }
            }
        }
        return exec_.Execute(stmt, plan);
    }

    // Write path: log → apply → commit.
    bool is_implicit = (active_tid == -1);
    txn_id_t tid = is_implicit ? txm_->Begin() : active_tid;

    if (stmt.type == StmtType::INSERT) {
        if (!lm_->LockExclusive(tid, stmt.id)) {
            txm_->Abort(tid);
            ExecResult r;
            r.success = false;
            r.message = "ERROR: Deadlock / Lock Timeout occurred, transaction aborted.";
            return r;
        }
        LogRecord lr; lr.txn_id=tid; lr.type=LogType::INSERT;
        lr.record_id=stmt.id; lr.record_val=stmt.value;
        wal_->Append(lr);
    } else if (stmt.type == StmtType::DELETE) {
        if (!lm_->LockExclusive(tid, stmt.id)) {
            txm_->Abort(tid);
            ExecResult r;
            r.success = false;
            r.message = "ERROR: Deadlock / Lock Timeout occurred, transaction aborted.";
            return r;
        }
        LogRecord lr; lr.txn_id=tid; lr.type=LogType::DELETE;
        lr.record_id=stmt.id;
        wal_->Append(lr);
    }

    ExecResult r = exec_.Execute(stmt, plan);

    if (is_implicit) {
        txm_->Commit(tid); // logs COMMIT and flushes WAL
    }
    return r;
}

} // namespace minidb
