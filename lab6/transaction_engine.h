// transaction_engine.h — ADBMS Lab 6 / 24bcs10213 Jatin Chulet
//
// In-memory transaction processing simulator implementing:
//   * MVCC for reads: Readers fetch a snapshot taken at transaction start without blocking.
//   * Strict 2PL for writes: Writers acquire exclusive locks on records until commit/rollback.
//   * Deadlock Management: Detects cycles in a wait-graph and aborts the youngest transaction.
//   * Concurrency Control: Prevents dirty overwrites (first-updater-wins strategy).
//   * Garbage Collection: Safely removes outdated record versions no longer visible to any active snapshot.

#ifndef JATIN_ADBMS_LAB8_ENGINE_H
#define JATIN_ADBMS_LAB8_ENGINE_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace db_core {

using TransactionID = unsigned long long;

enum class OpStatus { Success, Missing, Waiting, RolledBack, ConflictError };
const char* status_to_string(OpStatus st);

enum class State { Running, Saved, Failed };

class TransactionEngine {
public:
    TransactionEngine() = default;

    TransactionID start_tx();

    // MVCC snapshot read (includes uncommitted local writes)
    OpStatus fetch_record(TransactionID tid, const std::string& rec_key, std::string& result);

    // 2PL write operations
    OpStatus update_record(TransactionID tid, const std::string& rec_key, const std::string& val);
    OpStatus delete_record(TransactionID tid, const std::string& rec_key);

    // Transaction lifecycle
    OpStatus commit_tx(TransactionID tid);
    void rollback_tx(TransactionID tid);

    // Maintenance
    std::size_t cleanup_garbage();

    // Inspection tools for testing
    State get_state(TransactionID tid) const;
    TransactionID get_latest_aborted() const { return latest_aborted_; }
    std::size_t get_total_versions() const;

private:
    struct RecordVersion {
        std::string data;
        bool is_tombstone = false;
        TransactionID start_time = 0;
        TransactionID end_time = 0;
        TransactionID author_tx = 0;
    };

    struct BufferedWrite {
        std::string data;
        bool is_tombstone;
    };

    struct TransactionContext {
        TransactionID tx_id = 0;
        TransactionID view_ts = 0;
        State current_status = State::Running;
        std::unordered_map<std::string, BufferedWrite> pending_ops;
        std::unordered_set<std::string> held_locks;
    };

    TransactionID id_generator_ = 1;
    TransactionID global_timer_ = 0;
    TransactionID latest_aborted_ = 0;

    std::unordered_map<TransactionID, TransactionContext> active_transactions_;
    std::unordered_map<std::string, std::vector<RecordVersion>> data_store_;
    std::unordered_map<std::string, TransactionID> exclusive_locks_;
    std::unordered_map<TransactionID, TransactionID> wait_graph_;

    const RecordVersion* find_visible_version(const std::string& rec_key, TransactionID view_ts) const;
    OpStatus try_lock(TransactionContext& ctx, const std::string& rec_key);
    void free_locks(TransactionContext& ctx);
    void clear_wait_edges(TransactionID tid);
    void force_rollback(TransactionID tid);
};

}  // namespace db_core

#endif // JATIN_ADBMS_LAB6_ENGINE_H