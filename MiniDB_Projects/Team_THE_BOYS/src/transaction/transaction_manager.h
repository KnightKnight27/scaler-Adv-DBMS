#pragma once

#include <optional>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "common/types.h"
#include "recovery/wal.h"
#include "transaction/lock_manager.h"

namespace minidb {

struct UndoEntry {
    enum class Op { INSERT, DELETE_TUP } op;
    std::string table;
    Row row;
    Rid rid;
};

class TransactionManager {
public:
    TransactionManager(LockManager* lock_manager, WriteAheadLog* wal);

    void Begin();
    void Commit();
    void Rollback();
    bool InTransaction() const { return active_txn_.has_value(); }
    int CurrentTxnId() const { return active_txn_.value_or(-1); }

    void LockTable(const std::string& table, LockMode mode);
    void LogInsert(const std::string& table, const Row& row, const Rid& rid);
    void LogDelete(const std::string& table, const Row& row, const Rid& rid);
    void CommitIfAuto();
    bool IsExplicitTransaction() const { return explicit_txn_; }

    const std::vector<UndoEntry>& undo_log() const { return undo_log_; }
    void ClearUndoLog() { undo_log_.clear(); }

private:
    LockManager* lock_manager_;
    WriteAheadLog* wal_;
    std::optional<int> active_txn_;
    bool explicit_txn_ = false;
    int next_txn_id_ = 1;
    std::vector<UndoEntry> undo_log_;
};

}  // namespace minidb
