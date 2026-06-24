#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Lab 8 — Timestamp MVCC + Strict 2PL + Deadlock Detection
// Modular layout: engine header + implementation + demo driver (main.cpp)
// ---------------------------------------------------------------------------

enum class TxStatus { Running, Waiting, Committed, Aborted };

enum class LockType { Shared, Exclusive };

struct RowVersion {
    int value;
    int author_tx;    // which transaction created this version
    int commit_time;  // logical timestamp when version became visible
};

struct TxContext {
    int id = 0;
    int snap_time = 0;  // read snapshot: see versions with commit_time <= snap_time
    TxStatus status = TxStatus::Running;
    bool past_growing_phase = false;
    std::set<std::string> shared_keys;
    std::set<std::string> exclusive_keys;
    std::vector<std::pair<std::string, int>> staged_writes;
};

// Timestamp-ordered version chains (append-only, read newest visible version)
class VersionStore {
public:
    void bootstrap(const std::string& key, int value);
    int current_time() const;
    int read_visible(const std::string& key, const TxContext& tx) const;
    void apply_commit(TxContext& tx);
    void dump_chains() const;

private:
    std::map<std::string, std::vector<RowVersion>> table_;
    int logical_clock_ = 0;
};

// Strict two-phase locking with immediate deadlock abort (no blocking sleep)
class TwoPhaseLockTable {
public:
    bool request(TxContext& tx, const std::string& key, LockType type);
    void drop_all(TxContext& tx);
    void force_abort(TxContext& tx);
    void dump_waits() const;

private:
    struct ResourceLock {
        std::set<int> shared_holders;
        int exclusive_holder = -1;
    };

    std::map<std::string, ResourceLock> resources_;
    std::map<int, std::set<int>> wait_edges_;  // waiter -> blockers

    std::set<int> find_blockers(int tx_id, const ResourceLock& res, LockType type) const;
    void grant(TxContext& tx, const std::string& key, ResourceLock& res, LockType type);
    bool graph_has_cycle() const;
    bool dfs_cycle(int node, std::set<int>& seen, std::set<int>& path) const;
    void scrub_wait_edges(int tx_id);
};

class DbEngine {
public:
    DbEngine();

    TxContext open_txn();
    void read(TxContext& tx, const std::string& key);
    void write(TxContext& tx, const std::string& key, int value);
    void commit(TxContext& tx);
    void print_state() const;

private:
    VersionStore store_;
    TwoPhaseLockTable locks_;
    int next_id_ = 1;
};
