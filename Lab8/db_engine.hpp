#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Lab 8 - Advanced Database Engine Simulation
// Features: MVCC with Logical Timestamps, Strict 2PL, Cycle Detection
// ---------------------------------------------------------------------------

enum class TransactionState { Active, Blocked, Success, Failed };

enum class LockMode { Read, Write };

struct RecordVersion {
    int data_val;
    int creator_id;
    int ts_commit;
};

struct TransactionHandle {
    int tx_id = 0;
    int snapshot_ts = 0;
    TransactionState state = TransactionState::Active;
    bool shrink_phase = false;
    std::set<std::string> read_locks;
    std::set<std::string> write_locks;
    std::vector<std::pair<std::string, int>> pending_writes;
};

class MVCCStore {
public:
    void insert_initial(const std::string& key, int val);
    int get_clock() const;
    int fetch_version(const std::string& key, const TransactionHandle& tx) const;
    void commit_writes(TransactionHandle& tx);
    void print_versions() const;

private:
    std::map<std::string, std::vector<RecordVersion>> records_;
    int current_time_ = 0;
};

class LockManager {
public:
    bool acquire_lock(TransactionHandle& tx, const std::string& key, LockMode mode);
    void release_all_locks(TransactionHandle& tx);
    void rollback(TransactionHandle& tx);
    void print_wait_graph() const;

private:
    struct LockEntry {
        std::set<int> readers;
        int writer = -1;
    };

    std::map<std::string, LockEntry> lock_table_;
    std::map<int, std::set<int>> dependencies_;

    std::set<int> get_conflicts(int t_id, const LockEntry& entry, LockMode mode) const;
    void assign_lock(TransactionHandle& tx, const std::string& key, LockEntry& entry, LockMode mode);
    bool check_for_cycles() const;
    bool detect_cycle_dfs(int current, std::set<int>& visited, std::set<int>& call_stack) const;
    void remove_waiter(int t_id);
};

class DatabaseSystem {
public:
    DatabaseSystem();

    TransactionHandle begin_transaction();
    void perform_read(TransactionHandle& tx, const std::string& key);
    void perform_write(TransactionHandle& tx, const std::string& key, int val);
    void perform_commit(TransactionHandle& tx);
    void display_status() const;

private:
    MVCCStore mvcc_;
    LockManager lock_mgr_;
    int id_generator_ = 1;
};
