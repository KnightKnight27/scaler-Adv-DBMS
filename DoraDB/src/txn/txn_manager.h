#pragma once

#include "txn/lock_manager.h"
#include "common/types.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <mutex>

// ============================================================
// TxnManager — transaction lifecycle management (Strict 2PL)
//
// Begin → acquire locks via LockManager → Commit/Abort
// On abort: undo writes using stored before-images.
// All locks held until commit or abort (strict 2PL guarantee).
// ============================================================

enum class TxnState { ACTIVE, COMMITTED, ABORTED };

// A record of one write operation (for undo on abort)
struct WriteRecord {
    std::string table;
    RID rid;
    bool was_insert;       // true = INSERT (undo = delete), false = UPDATE (undo = restore)
    std::vector<char> before_image;  // original row data (empty for inserts)
    int before_size = 0;
};

struct Transaction {
    int txn_id;
    TxnState state = TxnState::ACTIVE;
    std::vector<WriteRecord> write_set;  // for undo on abort
};

class TxnManager {
public:
    TxnManager(LockManager* lock_mgr);

    // Start a new transaction. Returns txn_id.
    int Begin();

    // Commit: release all locks, mark committed.
    void Commit(int txn_id);

    // Abort: undo all writes, release all locks, mark aborted.
    // undo_fn is called for each write record to actually undo it.
    void Abort(int txn_id, std::function<void(const WriteRecord&)> undo_fn = nullptr);

    // Record a write (for undo on abort)
    void AddWriteRecord(int txn_id, const WriteRecord& record);

    // Get transaction state
    TxnState GetState(int txn_id);

    // Get the transaction object
    Transaction* GetTxn(int txn_id);

    LockManager* GetLockManager() { return lock_mgr_; }

private:
    LockManager* lock_mgr_;
    std::mutex mu_;
    int next_txn_id_ = 1;
    std::unordered_map<int, Transaction> transactions_;
};
