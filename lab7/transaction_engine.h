// tx_manager.h — ADBMS Lab 7 / 24bcs10632 Patel Jash
//
// In-memory transaction processing simulator implementing:
//   * MVCC for reads: Readers fetch a snapshot taken at transaction start without blocking.
//   * Strict 2PL for writes: Writers acquire exclusive locks on records until commit/rollback.
//   * Deadlock Management: Detects cycles in a wait-graph and aborts the youngest transaction.
//   * Concurrency Control: Prevents dirty overwrites (first-updater-wins strategy).
//   * Garbage Collection: Safely removes outdated record versions no longer visible to any active snapshot.

#ifndef PATEL_ADBMS_LAB7_TX_MANAGER_H
#define PATEL_ADBMS_LAB7_TX_MANAGER_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace database_core {

using TxID = unsigned long long;

enum class EngineStatus { Ok, NotFound, Blocked, Aborted, SerializationError };
const char* engine_status_to_string(EngineStatus st);

enum class TxState { Active, Committed, Terminated };

class TxManager {
public:
    TxManager() = default;

    TxID begin_transaction();

    // MVCC snapshot read (includes uncommitted local writes)
    EngineStatus read_key(TxID tid, const std::string& key, std::string& out_value);

    // 2PL write operations
    EngineStatus write_key(TxID tid, const std::string& key, const std::string& val);
    EngineStatus remove_key(TxID tid, const std::string& key);

    // Transaction lifecycle
    EngineStatus commit(TxID tid);
    void abort_transaction(TxID tid);

    // Maintenance
    std::size_t run_vacuum();

    // Inspection tools for testing
    TxState get_transaction_status(TxID tid) const;
    TxID get_last_killed() const { return last_killed_tx_; }
    std::size_t count_all_versions() const;

private:
    struct DataVersion {
        std::string value;
        bool is_deleted = false;
        TxID created_at = 0;
        TxID expired_at = 0;
        TxID writer_id = 0;
    };

    struct PendingWrite {
        std::string value;
        bool is_deleted;
    };

    struct TxContext {
        TxID id = 0;
        TxID snapshot_time = 0;
        TxState status = TxState::Active;
        std::unordered_map<std::string, PendingWrite> local_writes;
        std::unordered_set<std::string> acquired_locks;
    };

    TxID next_tx_id_ = 1;
    TxID system_clock_ = 0;
    TxID last_killed_tx_ = 0;

    std::unordered_map<TxID, TxContext> running_txs_;
    std::unordered_map<std::string, std::vector<DataVersion>> kv_store_;
    std::unordered_map<std::string, TxID> write_locks_;
    std::unordered_map<TxID, TxID> dependency_graph_;

    const DataVersion* locate_visible_version(const std::string& key, TxID view_ts) const;
    EngineStatus attempt_lock(TxContext& ctx, const std::string& key);
    void release_all_locks(TxContext& ctx);
    void prune_wait_dependencies(TxID tid);
    void execute_rollback(TxID tid);
};

}  // namespace database_core

#endif // PATEL_ADBMS_LAB7_TX_MANAGER_H