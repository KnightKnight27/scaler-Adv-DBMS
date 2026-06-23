#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <memory>
#include <mutex>
#include <condition_variable>

enum class TransactionStatus {
    ACTIVE,
    COMMITTED,
    ABORTED
};

// Represents a Transaction descriptor in the system
struct Transaction {
    int tx_id;
    TransactionStatus status;
    std::set<int> active_at_start; // Set of active transaction IDs when this transaction began (snapshot)
};

// Represents a single version of a database record
struct RecordVersion {
    std::string value;
    int xmin; // Transaction ID that inserted/created this version
    int xmax; // Transaction ID that deleted/superseded this version (0 if still active)
    std::shared_ptr<RecordVersion> prev; // Pointer to the previous (older) version

    RecordVersion(const std::string& val, int x_in, std::shared_ptr<RecordVersion> p = nullptr)
        : value(val), xmin(x_in), xmax(0), prev(p) {}
};

// Represents a database record containing a key and the head of its version chain (newest first)
struct Record {
    int key;
    std::shared_ptr<RecordVersion> latest_version;
};

// Manages lock acquisition (Strict 2PL) and Wait-For Graph cycle checks
class LockManager {
private:
    std::unordered_map<int, int> lock_table; // key -> holding tx_id (0 if unlocked)
    std::unordered_map<int, std::vector<int>> wait_table; // key -> list of waiting tx_ids
    std::unordered_map<int, int> tx_waiting_on_key; // tx_id -> key it is currently blocked on
    std::unordered_map<int, std::set<int>> wait_for_graph; // WFG adjacency list: tx_id -> set of tx_ids it waits on
    
    std::mutex lk_mtx;
    std::unordered_map<int, std::shared_ptr<std::condition_variable>> cv_table;

    // Helper to detect cycle using Depth First Search
    bool detectCycleDFS(int node, std::unordered_map<int, bool>& visited, std::unordered_map<int, bool>& recStack, std::vector<int>& path);

public:
    // Build the Wait-For Graph based on blocked transactions and lock holders
    void buildGraph();

    // Perform deadlock detection. Returns tx_id of the victim to abort, or 0 if no cycle.
    int detectDeadlock();

    // Request an exclusive write lock. Blocks thread if held. Returns false if abort occurs.
    bool acquireLock(int tx_id, int key);

    // Release all locks held by a transaction. Wakes up waiting transactions.
    void releaseLocksForTx(int tx_id);

    // Prints WFG structure to stdout
    void printWFG();
};

class MVCCDatabase {
private:
    std::unordered_map<int, Record> db;
    std::unordered_map<int, Transaction> transactions;
    int next_tx_id = 1;
    LockManager lock_mgr;
    std::mutex db_mtx;

    // Helper methods for MVCC visibility check
    bool isTxCommitted(int tx_id) const;
    bool isTxActive(int tx_id) const;
    bool isVisible(const std::shared_ptr<RecordVersion>& version, const Transaction& tx) const;

public:
    MVCCDatabase();

    // Transaction Management operations
    int beginTransaction();
    bool commitTransaction(int tx_id);
    void abortTransaction(int tx_id);

    // Database access operations
    bool readRecord(int tx_id, int key, std::string& out_value);
    bool writeRecord(int tx_id, int key, const std::string& value);

    // Helper to display current state of database and version chains
    void printDatabaseState();
};
