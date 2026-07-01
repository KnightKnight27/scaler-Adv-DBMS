#ifndef MVCC_LEDGER_ENGINE_H
#define MVCC_LEDGER_ENGINE_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mvcc_ledger
{

  // Standard type for unique transaction sequence identifiers
  using TxID = unsigned long long;

  // Execution status codes for ledger operations
  enum class ExecStatus
  {
    Success,
    KeyMissing,
    WaitBlocked,
    ForceAborted,
    CommitConflict
  };

  // Stringifier for debugging output
  std::string formatStatus(ExecStatus status);

  // Represents the overall state of a transaction in the system
  enum class Lifecycle
  {
    InProgress,
    Finalized,
    RolledBack
  };

  class LedgerCoordinator
  {
  public:
    LedgerCoordinator() = default;

    // ------------------------------------------------------------------------
    // Transaction Lifecycle Management
    // ------------------------------------------------------------------------
    TxID beginTransaction();
    ExecStatus commitTransaction(TxID tid);
    void rollbackTransaction(TxID tid);

    // ------------------------------------------------------------------------
    // Read / Write Operations
    // ------------------------------------------------------------------------

    // Reads an account balance using the transaction's frozen MVCC snapshot
    ExecStatus readRecord(TxID tid, const std::string &accountKey, std::string &outBalance);

    // Stages an update and requests a Strict 2PL exclusive lock on the asset
    ExecStatus updateRecord(TxID tid, const std::string &accountKey, const std::string &newBalance);
    ExecStatus deleteRecord(TxID tid, const std::string &accountKey);

    // ------------------------------------------------------------------------
    // Engine Maintenance & Diagnostics
    // ------------------------------------------------------------------------

    // Sweeps the ledger and drops asset versions no longer visible to any active transaction
    std::size_t collectGarbage();

    Lifecycle queryTxLifecycle(TxID tid) const;
    TxID getLastAbortedTx() const { return latest_victim_tx_; }
    std::size_t getTotalStoredVersions() const;

  private:
    // ------------------------------------------------------------------------
    // Internal Data Structures
    // ------------------------------------------------------------------------

    // Represents a single historical immutable state of an asset/account
    struct AssetVersion
    {
      std::string balance;
      bool is_closed = false;
      TxID mint_timestamp = 0;
      TxID burn_timestamp = 0;
      TxID origin_tx = 0;
    };

    // Represents a mutation held in a transaction's local workspace before commit
    struct StagedMutation
    {
      std::string staged_balance;
      bool is_closed;
    };

    // Maintains the workspace, locks, and MVCC snapshot timestamp for an active user
    struct LedgerSession
    {
      TxID session_id = 0;
      TxID view_timestamp = 0;
      Lifecycle state = Lifecycle::InProgress;

      std::unordered_map<std::string, StagedMutation> local_mutations;
      std::unordered_set<std::string> held_locks;
    };

    // ------------------------------------------------------------------------
    // Engine State
    // ------------------------------------------------------------------------

    TxID tx_sequence_tracker_ = 1;
    TxID global_clock_ = 0;
    TxID latest_victim_tx_ = 0;

    // Active workspaces mapped by their TxID
    std::unordered_map<TxID, LedgerSession> active_sessions_;

    // The actual Multi-Version database: Maps asset keys to a historical timeline of states
    std::unordered_map<std::string, std::vector<AssetVersion>> historical_ledger_;

    // Strict 2PL Lock Table: Maps asset keys to the TxID that currently holds the exclusive lock
    std::unordered_map<std::string, TxID> exclusive_locks_;

    // Dependency Graph for Deadlock Detection: Maps (Waiting Tx) -> (Blocking Tx)
    std::unordered_map<TxID, TxID> wait_for_graph_;

    // ------------------------------------------------------------------------
    // Internal Helper Methods
    // ------------------------------------------------------------------------

    const AssetVersion *fetchSnapshot(const std::string &key, TxID timestamp) const;
    ExecStatus requestWriteLock(LedgerSession &session, const std::string &key);

    void dropAllLocks(LedgerSession &session);
    void clearWaitGraphEdges(TxID tid);
    void executeTeardown(TxID tid);
  };

}

#endif