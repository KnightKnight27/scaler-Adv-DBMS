#pragma once
#include "value.h"
#include "engine.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ─── Transaction IDs and timestamps ─────────────────────────────────────────
using TxnId = uint64_t;
using Timestamp = uint64_t;    // logical clock; monotonically increasing

// ─── WAL log record types ─────────────────────────────────────────────────────
enum class LogType : uint8_t {
    BEGIN   = 0,
    INSERT  = 1,
    DELETE  = 2,
    COMMIT  = 3,
    ABORT   = 4,
};

struct LogRecord {
    LogType    type;
    TxnId      txn_id;
    std::string table;
    Row        row;     // the row affected (before-image for DELETE, after for INSERT)
    RID        rid;
};

// ─── WAL (Write-Ahead Log) ───────────────────────────────────────────────────
// Appends log records to a file before applying changes to the heap.
// On recovery, REDO all committed txns and UNDO all aborted/in-flight txns.
class WAL {
public:
    explicit WAL(const std::string& path);
    ~WAL();

    void append(const LogRecord& rec);
    void flush();

    // Read all records in order for crash recovery
    std::vector<LogRecord> read_all() const;

private:
    std::string   path_;
    std::ofstream out_;
};

// ─── 2PL Lock Manager ────────────────────────────────────────────────────────
//
// Strict two-phase locking: transactions acquire locks during execution and
// release ALL locks only at commit or abort.
//
// Resources are identified by (table, pk_value) strings.
// Lock modes: SHARED (read) and EXCLUSIVE (write).
// Deadlock is detected by building a waits-for graph and checking for cycles.
// ─────────────────────────────────────────────────────────────────────────────
enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxnId    txn_id;
    LockMode mode;
    bool     granted = false;
};

struct LockTable {
    std::vector<LockRequest> queue;   // granted requests come first
    std::mutex               mu;
    std::condition_variable  cv;
};

class LockManager {
public:
    // Acquire lock; blocks if a conflicting lock is held.
    // Throws DeadlockException if a cycle is detected.
    void lock(TxnId txn_id, const std::string& resource, LockMode mode);

    // Release all locks held by txn_id (called on commit or abort)
    void unlock_all(TxnId txn_id);

    // Returns resources locked by this transaction (for diagnostics)
    std::vector<std::string> held_by(TxnId txn_id) const;

private:
    std::map<std::string, LockTable>  lock_tables_;  // resource → lock table
    std::mutex                         global_mu_;    // guards lock_tables_

    // Waits-for tracking for deadlock detection
    std::unordered_map<TxnId, std::set<TxnId>> waits_for_;
    std::mutex                                   wf_mu_;

    void add_waits_for(TxnId waiter, TxnId holder);
    void remove_waits_for(TxnId waiter);
    bool has_cycle(TxnId start);                       // DFS cycle check

    bool conflicts(LockMode held, LockMode requested) const;
};

struct DeadlockException : std::exception {
    TxnId victim;
    explicit DeadlockException(TxnId v) : victim(v) {}
    const char* what() const noexcept override { return "Deadlock detected"; }
};

// ─── MVCC version chain ──────────────────────────────────────────────────────
//
// Each row has a version chain: a list of (value, begin_ts, end_ts) entries.
// A transaction with snapshot timestamp T sees versions where begin_ts <= T
// and end_ts > T.
// ─────────────────────────────────────────────────────────────────────────────
struct RowVersion {
    Row        data;
    Timestamp  begin_ts;    // the commit timestamp of the txn that created it
    Timestamp  end_ts;      // UINT64_MAX means "still current"
    TxnId      creator;     // which txn wrote this version
};

class MvccStore {
public:
    // Called when a transaction commits a new version of a row.
    // old_end_ts is set on the previous version; begin_ts on the new one.
    void put_version(const std::string& table, RID rid,
                     const Row& new_data, Timestamp begin_ts, TxnId creator);

    // Mark the current visible version of (table,rid) as deleted.
    void delete_version(const std::string& table, RID rid,
                        Timestamp end_ts);

    // Read the version of (table, rid) visible to snapshot_ts.
    // Returns nullopt if the row was never written or was deleted before snapshot_ts.
    std::optional<Row> read(const std::string& table, RID rid,
                            Timestamp snapshot_ts) const;

    // Scan all current versions visible to snapshot_ts for a table
    std::vector<std::pair<RID, Row>> scan(const std::string& table,
                                          Timestamp snapshot_ts) const;

    void add_rid_for_table(const std::string& table, RID rid);

private:
    // table → {rid → version_chain}
    std::map<std::string,
             std::map<RID, std::vector<RowVersion>>> chains_;
    // table → ordered list of all RIDs seen (for scan)
    std::map<std::string, std::vector<RID>> rids_;
    mutable std::mutex mu_;
};

// ─── Transaction descriptor ──────────────────────────────────────────────────
enum class TxnState { ACTIVE, COMMITTED, ABORTED };

// One reversible action recorded during a transaction.
//   op == INSERT  → to undo, DELETE this row
//   op == DELETE  → to undo, re-INSERT this row (before-image stored in `row`)
struct UndoEntry {
    LogType     op;
    std::string table;
    RID         rid;
    Row         row;
};

struct Transaction {
    TxnId       id;
    Timestamp   snapshot_ts;   // MVCC snapshot (begin timestamp)
    TxnState    state = TxnState::ACTIVE;
    std::vector<UndoEntry> undo_log;  // applied in reverse on abort
};

// ─── TransactionManager ──────────────────────────────────────────────────────
// Provides two concurrency control modes that can be compared:
//   MODE_2PL  — strict two-phase locking (readers block writers and vice-versa)
//   MODE_MVCC — multi-version concurrency control (readers never block writers)
enum class ConcurrencyMode { TWO_PL, MVCC };

class TransactionManager {
public:
    explicit TransactionManager(Database& db, const std::string& wal_path,
                                ConcurrencyMode mode = ConcurrencyMode::TWO_PL);

    // Start a new transaction; returns its TxnId
    TxnId begin();

    // Execute a SQL statement within transaction txn_id.
    // In 2PL mode: acquires locks before reading/writing.
    // In MVCC mode: reads from the snapshot, buffers writes.
    ResultSet execute(TxnId txn_id, const std::string& sql);

    void commit(TxnId txn_id);
    void abort(TxnId txn_id);

    ConcurrencyMode mode() const { return mode_; }

private:
    Database&         db_;
    LockManager       lm_;
    MvccStore         mvcc_;
    WAL               wal_;
    ConcurrencyMode   mode_;

    std::atomic<TxnId>      next_txn_id_{1};
    std::atomic<Timestamp>  clock_{1};
    std::map<TxnId, Transaction>  active_;
    std::mutex                     mu_;

    // 2PL helpers
    void tpl_execute_insert(TxnId txn_id, const InsertStmt& s, Transaction& txn);
    void tpl_execute_delete(TxnId txn_id, const DeleteStmt& s, Transaction& txn);

    // MVCC helpers
    ResultSet mvcc_execute_select(TxnId txn_id, const SelectStmt& s, Transaction& txn);
    void      mvcc_execute_insert(TxnId txn_id, const InsertStmt& s, Transaction& txn);
    void      mvcc_execute_delete(TxnId txn_id, const DeleteStmt& s, Transaction& txn);

    Timestamp next_ts() { return clock_.fetch_add(1); }
    std::string resource_key(const std::string& table, const Value& pk) const;
};

// Crash recovery: reads the WAL and replays/undoes as needed
void recover(Database& db, const std::string& wal_path);
