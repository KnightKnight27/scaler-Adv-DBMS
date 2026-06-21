// ============================================================================
// Lab 8: Transaction Manager
//        MVCC Version Chains + Strict 2PL + Deadlock Detection
//
// Components:
//   1. MVCC Version Chain — each data item maintains a linked list of versions
//      tagged with (txn_id, begin_ts, end_ts) for snapshot isolation reads
//   2. Strict Two-Phase Locking (S2PL) — shared (S) and exclusive (X) locks
//      held until transaction commits/aborts (strict = locks released at end)
//   3. Deadlock Detection — wait-for graph construction + DFS cycle detection
//   4. Interactive demo with step-by-step visual output
// ============================================================================

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <queue>
#include <stack>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <functional>
#include <cassert>
#include <climits>

using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// Global timestamp counter (simulates a monotonic clock)
// ─────────────────────────────────────────────────────────────────────────────
static int globalTimestamp = 0;
int nextTimestamp() { return ++globalTimestamp; }

// ─────────────────────────────────────────────────────────────────────────────
// Part 1 — MVCC Version Chains
//
// Each data item (identified by a string key) has a chain of versions.
// A version stores:
//   - value        : the data value at this version
//   - created_by   : txn_id that wrote it
//   - begin_ts     : timestamp when this version became visible
//   - end_ts       : timestamp when this version was superseded (INT_MAX if current)
// ─────────────────────────────────────────────────────────────────────────────

struct Version {
    int    value;
    int    created_by;   // transaction ID that created this version
    int    begin_ts;     // visible from this timestamp
    int    end_ts;       // visible until this timestamp (INT_MAX = current)
    Version* prev;       // pointer to older version (version chain)

    Version(int val, int txnId, int bts)
        : value(val), created_by(txnId), begin_ts(bts),
          end_ts(INT_MAX), prev(nullptr) {}
};

class MVCCStore {
private:
    // key → head of version chain (most recent version first)
    unordered_map<string, Version*> heads;

public:
    ~MVCCStore() {
        for (auto& [key, head] : heads) {
            Version* v = head;
            while (v) {
                Version* old = v;
                v = v->prev;
                delete old;
            }
        }
    }

    // Write a new version of key
    void write(const string& key, int value, int txnId, int timestamp) {
        Version* newVer = new Version(value, txnId, timestamp);

        if (heads.find(key) != heads.end()) {
            Version* oldHead = heads[key];
            oldHead->end_ts = timestamp;   // old version expires at this timestamp
            newVer->prev = oldHead;
        }
        heads[key] = newVer;
    }

    // Read the version of key visible at the given snapshot timestamp
    // Returns {found, value}
    pair<bool, int> read(const string& key, int snapshotTs) const {
        auto it = heads.find(key);
        if (it == heads.end()) return {false, 0};

        Version* v = it->second;
        while (v) {
            if (v->begin_ts <= snapshotTs && snapshotTs < v->end_ts) {
                return {true, v->value};
            }
            v = v->prev;
        }
        return {false, 0};
    }

    // Abort: remove all versions created by the given transaction
    void rollback(int txnId) {
        vector<string> keysToClean;
        for (auto& [key, head] : heads) {
            keysToClean.push_back(key);
        }

        for (const string& key : keysToClean) {
            Version* v = heads[key];
            Version* prev = nullptr;

            // Walk the chain and remove versions by txnId
            while (v && v->created_by == txnId) {
                Version* toDelete = v;
                v = v->prev;
                if (v) v->end_ts = INT_MAX; // restore previous version as current
                delete toDelete;
            }
            if (v) {
                heads[key] = v;
            } else {
                heads.erase(key);
            }
        }
    }

    // Display the version chain for a given key
    void displayChain(const string& key) const {
        auto it = heads.find(key);
        if (it == heads.end()) {
            cout << "  (no versions)" << endl;
            return;
        }

        Version* v = it->second;
        int depth = 0;
        while (v) {
            string endStr = (v->end_ts == INT_MAX) ? "∞ (current)" : to_string(v->end_ts);
            cout << "  ";
            if (depth == 0)
                cout << "HEAD → ";
            else
                cout << "       ";

            cout << "[val=" << v->value
                 << ", txn=T" << v->created_by
                 << ", ts=[" << v->begin_ts << ", " << endStr << ")"
                 << "]";

            if (v->prev) cout << " → ";
            cout << endl;

            v = v->prev;
            depth++;
        }
    }

    // Display all version chains
    void displayAll() const {
        if (heads.empty()) {
            cout << "  (store is empty)" << endl;
            return;
        }
        // Sort keys for consistent output
        vector<string> keys;
        for (auto& [k, _] : heads) keys.push_back(k);
        sort(keys.begin(), keys.end());

        for (const string& key : keys) {
            cout << "  Key '" << key << "':" << endl;
            displayChain(key);
        }
    }

    bool hasKey(const string& key) const {
        return heads.find(key) != heads.end();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Part 2 — Lock Manager (Strict 2PL)
//
// Lock modes: SHARED (S) — multiple readers allowed
//             EXCLUSIVE (X) — single writer, no readers
//
// Strict 2PL: locks are acquired as needed, but ALL locks are held until
// the transaction commits or aborts (no early release).
//
// Lock compatibility matrix:
//         |  S  |  X  |
//   S     | YES | NO  |
//   X     | NO  | NO  |
// ─────────────────────────────────────────────────────────────────────────────

enum class LockMode { SHARED, EXCLUSIVE };

string lockModeStr(LockMode m) {
    return m == LockMode::SHARED ? "S" : "X";
}

struct LockRequest {
    int      txnId;
    LockMode mode;
    bool     granted;

    LockRequest(int t, LockMode m, bool g = false)
        : txnId(t), mode(m), granted(g) {}
};

class LockManager {
private:
    // key → list of lock requests (granted + waiting)
    unordered_map<string, vector<LockRequest>> lockTable;

    // Track which keys each transaction holds locks on
    unordered_map<int, set<string>> txnLocks;

    bool isCompatible(LockMode requested, const vector<LockRequest>& existing) const {
        for (const auto& req : existing) {
            if (!req.granted) continue;  // skip waiting requests

            if (requested == LockMode::EXCLUSIVE) return false; // X conflicts with any
            if (req.mode == LockMode::EXCLUSIVE) return false;  // S conflicts with X
        }
        return true;
    }

public:
    // Attempt to acquire a lock. Returns true if granted, false if must wait.
    bool acquire(int txnId, const string& key, LockMode mode) {
        auto& requests = lockTable[key];

        // Check if this txn already holds a lock on this key
        for (auto& req : requests) {
            if (req.txnId == txnId && req.granted) {
                // Already hold a lock — check if upgrade needed
                if (req.mode == LockMode::EXCLUSIVE || mode == LockMode::SHARED) {
                    return true; // Already have equal or stronger lock
                }
                // Upgrade S → X: check compatibility with OTHER holders
                bool canUpgrade = true;
                for (auto& other : requests) {
                    if (other.txnId != txnId && other.granted) {
                        canUpgrade = false;
                        break;
                    }
                }
                if (canUpgrade) {
                    req.mode = LockMode::EXCLUSIVE;
                    return true;
                }
                // Can't upgrade — add as waiting
                requests.push_back(LockRequest(txnId, mode, false));
                return false;
            }
        }

        // New lock request
        if (isCompatible(mode, requests)) {
            requests.push_back(LockRequest(txnId, mode, true));
            txnLocks[txnId].insert(key);
            return true;
        }

        // Must wait
        requests.push_back(LockRequest(txnId, mode, false));
        return false;
    }

    // Release ALL locks held by a transaction (Strict 2PL — release at commit/abort)
    // Also removes any pending (waiting) lock requests from this txn.
    void releaseAll(int txnId) {
        // First, collect all keys where this txn has any entry (granted or waiting)
        set<string> allKeys;

        // Keys from granted locks
        auto it = txnLocks.find(txnId);
        if (it != txnLocks.end()) {
            allKeys = it->second;
        }

        // Also scan for waiting requests (not tracked in txnLocks)
        for (auto& [key, requests] : lockTable) {
            for (auto& req : requests) {
                if (req.txnId == txnId) {
                    allKeys.insert(key);
                    break;
                }
            }
        }

        for (const string& key : allKeys) {
            auto& requests = lockTable[key];

            // Remove ALL this txn's entries (granted AND waiting)
            requests.erase(
                remove_if(requests.begin(), requests.end(),
                          [txnId](const LockRequest& r) { return r.txnId == txnId; }),
                requests.end());

            // Try to grant waiting requests
            grantWaiting(key);

            if (requests.empty()) {
                lockTable.erase(key);
            }
        }

        txnLocks.erase(txnId);
    }

    // After a release, try to grant waiting requests
    void grantWaiting(const string& key) {
        auto it = lockTable.find(key);
        if (it == lockTable.end()) return;
        auto& requests = it->second;

        for (auto& req : requests) {
            if (req.granted) continue;

            // Check if this waiting request is now compatible with granted ones
            bool compatible = true;
            for (auto& other : requests) {
                if (!other.granted) continue;
                if (req.mode == LockMode::EXCLUSIVE || other.mode == LockMode::EXCLUSIVE) {
                    compatible = false;
                    break;
                }
            }
            if (compatible) {
                req.granted = true;
                txnLocks[req.txnId].insert(key);
            }
        }
    }

    // Get who is holding locks that block a given txn on a given key
    // (for building the wait-for graph)
    vector<int> getBlockers(int txnId, const string& key) const {
        vector<int> blockers;
        auto it = lockTable.find(key);
        if (it == lockTable.end()) return blockers;

        // Check if txnId is waiting
        bool isWaiting = false;
        LockMode waitingMode = LockMode::SHARED;
        for (auto& req : it->second) {
            if (req.txnId == txnId && !req.granted) {
                isWaiting = true;
                waitingMode = req.mode;
                break;
            }
        }
        if (!isWaiting) return blockers;

        for (auto& req : it->second) {
            if (req.txnId == txnId) continue;
            if (!req.granted) continue;
            // This granted lock conflicts with our request
            if (waitingMode == LockMode::EXCLUSIVE || req.mode == LockMode::EXCLUSIVE) {
                blockers.push_back(req.txnId);
            }
        }
        return blockers;
    }

    // Get all waiting txn → key pairs
    vector<pair<int, string>> getWaiters() const {
        vector<pair<int, string>> waiters;
        for (auto& [key, requests] : lockTable) {
            for (auto& req : requests) {
                if (!req.granted) {
                    waiters.push_back({req.txnId, key});
                }
            }
        }
        return waiters;
    }

    // Display the lock table
    void display() const {
        if (lockTable.empty()) {
            cout << "  (lock table is empty)" << endl;
            return;
        }

        vector<string> keys;
        for (auto& [k, _] : lockTable) keys.push_back(k);
        sort(keys.begin(), keys.end());

        cout << "  +" << string(10, '-') << "+" << string(40, '-') << "+" << endl;
        cout << "  | " << left << setw(9) << "Key"
             << "| " << setw(39) << "Lock Requests" << "|" << endl;
        cout << "  +" << string(10, '-') << "+" << string(40, '-') << "+" << endl;

        for (const string& key : keys) {
            auto it = lockTable.find(key);
            string reqStr;
            for (auto& req : it->second) {
                if (!reqStr.empty()) reqStr += ", ";
                reqStr += "T" + to_string(req.txnId) + ":" + lockModeStr(req.mode);
                reqStr += req.granted ? "(✓)" : "(wait)";
            }
            cout << "  | " << left << setw(9) << key
                 << "| " << setw(39) << reqStr << "|" << endl;
        }
        cout << "  +" << string(10, '-') << "+" << string(40, '-') << "+" << endl;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Part 3 — Deadlock Detector (Wait-For Graph + DFS Cycle Detection)
//
// Build a directed graph:  T_i → T_j   means "T_i is waiting for T_j"
// A cycle in this graph indicates a deadlock.
// ─────────────────────────────────────────────────────────────────────────────

class DeadlockDetector {
private:
    // adjacency list: txnId → set of txnIds it waits for
    unordered_map<int, unordered_set<int>> waitForGraph;

    // DFS-based cycle detection
    bool dfsCycle(int node, unordered_set<int>& visited,
                  unordered_set<int>& recStack, vector<int>& cycle) const {
        visited.insert(node);
        recStack.insert(node);
        cycle.push_back(node);

        auto it = waitForGraph.find(node);
        if (it != waitForGraph.end()) {
            for (int neighbor : it->second) {
                if (recStack.count(neighbor)) {
                    // Found a cycle — trim the cycle vector to start from 'neighbor'
                    cycle.push_back(neighbor);
                    return true;
                }
                if (!visited.count(neighbor)) {
                    if (dfsCycle(neighbor, visited, recStack, cycle)) {
                        return true;
                    }
                }
            }
        }

        recStack.erase(node);
        cycle.pop_back();
        return false;
    }

public:
    void clear() { waitForGraph.clear(); }

    void addEdge(int from, int to) {
        waitForGraph[from].insert(to);
    }

    // Build wait-for graph from the lock manager state
    void buildFrom(const LockManager& lm) {
        clear();
        auto waiters = lm.getWaiters();
        for (auto& [txnId, key] : waiters) {
            auto blockers = lm.getBlockers(txnId, key);
            for (int blocker : blockers) {
                addEdge(txnId, blocker);
            }
        }
    }

    // Detect cycle. Returns the cycle as a list of txn IDs, or empty if none.
    vector<int> detectCycle() const {
        unordered_set<int> visited;
        unordered_set<int> recStack;

        for (auto& [node, _] : waitForGraph) {
            if (!visited.count(node)) {
                vector<int> cycle;
                if (dfsCycle(node, visited, recStack, cycle)) {
                    // Clean up cycle: find where the repeated node starts
                    int repeated = cycle.back();
                    vector<int> cleanCycle;
                    bool found = false;
                    for (int n : cycle) {
                        if (n == repeated && !found) found = true;
                        if (found) cleanCycle.push_back(n);
                    }
                    return cleanCycle;
                }
            }
        }
        return {}; // no deadlock
    }

    // Display the wait-for graph
    void display() const {
        if (waitForGraph.empty()) {
            cout << "  (no waits — graph is empty)" << endl;
            return;
        }

        // Collect all nodes
        set<int> allNodes;
        for (auto& [from, tos] : waitForGraph) {
            allNodes.insert(from);
            for (int to : tos) allNodes.insert(to);
        }

        cout << "  Wait-For Graph Edges:" << endl;
        for (auto& [from, tos] : waitForGraph) {
            for (int to : tos) {
                cout << "    T" << from << " ──waits──▶ T" << to << endl;
            }
        }

        // Visual adjacency
        cout << "\n  Adjacency Matrix:" << endl;
        vector<int> nodes(allNodes.begin(), allNodes.end());
        sort(nodes.begin(), nodes.end());

        cout << "       ";
        for (int n : nodes) cout << "T" << left << setw(4) << n;
        cout << endl;

        for (int from : nodes) {
            cout << "  T" << left << setw(4) << from;
            auto it = waitForGraph.find(from);
            for (int to : nodes) {
                bool edge = (it != waitForGraph.end() && it->second.count(to));
                cout << (edge ? " →  " : " ·  ");
            }
            cout << endl;
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Part 4 — Transaction Manager
//
// Coordinates MVCC reads/writes, lock acquisition (S2PL), and deadlock detection.
// ─────────────────────────────────────────────────────────────────────────────

enum class TxnState { ACTIVE, COMMITTED, ABORTED, WAITING };

string txnStateStr(TxnState s) {
    switch (s) {
        case TxnState::ACTIVE:    return "ACTIVE";
        case TxnState::COMMITTED: return "COMMITTED";
        case TxnState::ABORTED:   return "ABORTED";
        case TxnState::WAITING:   return "WAITING";
    }
    return "?";
}

struct Transaction {
    int      txnId;
    int      startTs;     // snapshot timestamp for MVCC reads
    TxnState state;
    vector<string> log;   // operation log for display

    Transaction(int id, int ts)
        : txnId(id), startTs(ts), state(TxnState::ACTIVE) {}
};

class TransactionManager {
private:
    MVCCStore       store;
    LockManager     lockMgr;
    DeadlockDetector detector;
    unordered_map<int, Transaction> transactions;
    int nextTxnId = 0;

public:
    // Begin a new transaction
    int begin() {
        int id = ++nextTxnId;
        int ts = nextTimestamp();
        transactions.emplace(id, Transaction(id, ts));
        transactions.at(id).log.push_back("BEGIN (snapshot_ts=" + to_string(ts) + ")");
        return id;
    }

    // Read a key within a transaction (MVCC snapshot read + S lock under S2PL)
    pair<bool, int> read(int txnId, const string& key) {
        auto it = transactions.find(txnId);
        if (it == transactions.end() ||
            (it->second.state != TxnState::ACTIVE && it->second.state != TxnState::WAITING)) {
            cout << "  ✗ T" << txnId << " is not active." << endl;
            return {false, 0};
        }
        Transaction& txn = it->second;

        // Acquire shared lock
        bool lockGranted = lockMgr.acquire(txnId, key, LockMode::SHARED);
        if (!lockGranted) {
            txn.state = TxnState::WAITING;
            txn.log.push_back("READ(" + key + ") → BLOCKED (waiting for S-lock)");
            cout << "  ⏳ T" << txnId << " BLOCKED on S-lock for '" << key << "'" << endl;
            return {false, 0};
        }

        // Lock acquired — if was WAITING, mark as ACTIVE again
        if (txn.state == TxnState::WAITING) txn.state = TxnState::ACTIVE;

        // MVCC read using snapshot timestamp
        auto [found, val] = store.read(key, txn.startTs);
        if (found) {
            txn.log.push_back("READ(" + key + ") = " + to_string(val) +
                              " [snapshot_ts=" + to_string(txn.startTs) + "]");
            cout << "  ✓ T" << txnId << " READ '" << key << "' = " << val
                 << " (snapshot @ts=" << txn.startTs << ")" << endl;
        } else {
            txn.log.push_back("READ(" + key + ") = NOT FOUND");
            cout << "  ✓ T" << txnId << " READ '" << key << "' = NOT FOUND" << endl;
        }
        return {found, val};
    }

    // Write a key within a transaction (X lock under S2PL + new MVCC version)
    bool write(int txnId, const string& key, int value) {
        auto it = transactions.find(txnId);
        if (it == transactions.end() ||
            (it->second.state != TxnState::ACTIVE && it->second.state != TxnState::WAITING)) {
            cout << "  ✗ T" << txnId << " is not active." << endl;
            return false;
        }
        Transaction& txn = it->second;

        // Acquire exclusive lock
        bool lockGranted = lockMgr.acquire(txnId, key, LockMode::EXCLUSIVE);
        if (!lockGranted) {
            txn.state = TxnState::WAITING;
            txn.log.push_back("WRITE(" + key + ", " + to_string(value) +
                              ") → BLOCKED (waiting for X-lock)");
            cout << "  ⏳ T" << txnId << " BLOCKED on X-lock for '" << key << "'" << endl;
            return false;
        }

        // Lock acquired — if was WAITING, mark as ACTIVE again
        if (txn.state == TxnState::WAITING) txn.state = TxnState::ACTIVE;

        // Create new MVCC version
        int writeTs = nextTimestamp();
        store.write(key, value, txnId, writeTs);
        txn.log.push_back("WRITE(" + key + ", " + to_string(value) +
                          ") [write_ts=" + to_string(writeTs) + "]");
        cout << "  ✓ T" << txnId << " WRITE '" << key << "' = " << value
             << " (version @ts=" << writeTs << ")" << endl;
        return true;
    }

    // Commit a transaction (release all locks under Strict 2PL)
    void commit(int txnId) {
        auto it = transactions.find(txnId);
        if (it == transactions.end()) {
            cout << "  ✗ T" << txnId << " not found." << endl;
            return;
        }
        Transaction& txn = it->second;
        txn.state = TxnState::COMMITTED;
        txn.log.push_back("COMMIT");
        lockMgr.releaseAll(txnId);
        cout << "  ✓ T" << txnId << " COMMITTED (all locks released)" << endl;
    }

    // Abort a transaction (rollback MVCC versions + release locks)
    void abort(int txnId) {
        auto it = transactions.find(txnId);
        if (it == transactions.end()) {
            cout << "  ✗ T" << txnId << " not found." << endl;
            return;
        }
        Transaction& txn = it->second;
        txn.state = TxnState::ABORTED;
        txn.log.push_back("ABORT (rollback)");
        store.rollback(txnId);
        lockMgr.releaseAll(txnId);
        cout << "  ✓ T" << txnId << " ABORTED (versions rolled back, locks released)" << endl;
    }

    // Run deadlock detection
    vector<int> detectDeadlock() {
        detector.buildFrom(lockMgr);
        return detector.detectCycle();
    }

    // Display current state
    void displayTransactions() const {
        cout << "\n  ┌──────────────────────────────────────────────────┐" << endl;
        cout << "  │           Transaction Status Table               │" << endl;
        cout << "  ├──────┬────────────┬─────────────────────────────┤" << endl;
        cout << "  │ " << left << setw(5) << "Txn"
             << "│ " << setw(11) << "State"
             << "│ " << setw(28) << "Snapshot TS" << "│" << endl;
        cout << "  ├──────┼────────────┼─────────────────────────────┤" << endl;

        // Sort by txnId
        vector<int> ids;
        for (auto& [id, _] : transactions) ids.push_back(id);
        sort(ids.begin(), ids.end());

        for (int id : ids) {
            const Transaction& txn = transactions.at(id);
            cout << "  │ T" << left << setw(4) << id
                 << "│ " << setw(11) << txnStateStr(txn.state)
                 << "│ " << setw(28) << txn.startTs << "│" << endl;
        }
        cout << "  └──────┴────────────┴─────────────────────────────┘" << endl;
    }

    void displayLockTable() const {
        cout << "\n  Lock Table:" << endl;
        lockMgr.display();
    }

    void displayVersionChains() const {
        cout << "\n  MVCC Version Chains:" << endl;
        store.displayAll();
    }

    void displayWaitForGraph() {
        detector.buildFrom(lockMgr);
        cout << "\n  Deadlock Detector — Wait-For Graph:" << endl;
        detector.display();
    }

    void displayTransactionLog(int txnId) const {
        auto it = transactions.find(txnId);
        if (it == transactions.end()) {
            cout << "  T" << txnId << " not found." << endl;
            return;
        }
        cout << "  Operation log for T" << txnId << ":" << endl;
        for (size_t i = 0; i < it->second.log.size(); i++) {
            cout << "    " << (i + 1) << ". " << it->second.log[i] << endl;
        }
    }

    // Accessors
    const LockManager& getLockManager() const { return lockMgr; }
    const MVCCStore& getStore() const { return store; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Part 5 — Demonstration Scenarios
// ─────────────────────────────────────────────────────────────────────────────

void printHeader(const string& title) {
    string border;
    for (int i = 0; i < 60; i++) border += "═";
    cout << "\n╔" << border << "╗" << endl;
    cout << "║  " << left << setw(58) << title << "║" << endl;
    cout << "╚" << border << "╝" << endl;
}

void printStep(int step, const string& desc) {
    cout << "\n── Step " << step << ": " << desc << " ──" << endl;
}

// Scenario 1: MVCC Version Chains — demonstrate snapshot isolation
void demoMVCCVersionChains() {
    printHeader("Scenario 1: MVCC Version Chains & Snapshot Isolation");

    cout << "\nThis demo shows how MVCC maintains multiple versions of data" << endl;
    cout << "so that readers see a consistent snapshot, even while writers" << endl;
    cout << "are creating new versions.\n" << endl;

    TransactionManager tm;
    int step = 0;

    printStep(++step, "T1 begins and writes A=10, B=20");
    int t1 = tm.begin();
    tm.write(t1, "A", 10);
    tm.write(t1, "B", 20);
    tm.commit(t1);
    tm.displayVersionChains();

    printStep(++step, "T2 begins (gets snapshot) → then T3 writes A=30");
    int t2 = tm.begin();
    cout << "  T2 started with snapshot — it should see A=10" << endl;
    int t3 = tm.begin();
    tm.write(t3, "A", 30);
    tm.commit(t3);
    tm.displayVersionChains();

    printStep(++step, "T2 reads A — should see OLD value (10) via snapshot isolation");
    tm.read(t2, "A");

    printStep(++step, "T4 begins AFTER T3's commit — reads A — should see 30");
    int t4 = tm.begin();
    tm.read(t4, "A");
    tm.commit(t4);
    tm.commit(t2);

    printStep(++step, "Final version chain for key 'A'");
    tm.displayVersionChains();

    printStep(++step, "Transaction status summary");
    tm.displayTransactions();
}

// Scenario 2: Strict 2PL — show lock acquisition and blocking
void demoStrict2PL() {
    printHeader("Scenario 2: Strict Two-Phase Locking (S2PL)");

    cout << "\nStrict 2PL ensures serializability by:" << endl;
    cout << "  1. Acquiring locks before reading (S) or writing (X)" << endl;
    cout << "  2. Holding ALL locks until COMMIT or ABORT" << endl;
    cout << "  3. Conflicting requests must wait\n" << endl;

    TransactionManager tm;
    int step = 0;

    printStep(++step, "T1 writes X=100 (acquires X-lock on 'X')");
    int t1 = tm.begin();
    tm.write(t1, "X", 100);
    tm.displayLockTable();

    printStep(++step, "T2 tries to read X (needs S-lock) — BLOCKED by T1's X-lock");
    int t2 = tm.begin();
    tm.read(t2, "X");
    tm.displayLockTable();

    printStep(++step, "T1 commits → releases all locks → T2's lock can be granted");
    tm.commit(t1);
    tm.displayLockTable();

    printStep(++step, "T2 can now read X (re-attempting)");
    tm.read(t2, "X");
    tm.displayLockTable();
    tm.commit(t2);

    printStep(++step, "Shared locks are compatible — T3 and T4 both read Y");
    int t3 = tm.begin();
    tm.write(t3, "Y", 200);
    tm.commit(t3);

    int t4 = tm.begin();
    int t5 = tm.begin();
    tm.read(t4, "Y");
    tm.read(t5, "Y");
    cout << "  Both T4 and T5 hold S-locks on Y simultaneously:" << endl;
    tm.displayLockTable();
    tm.commit(t4);
    tm.commit(t5);

    printStep(++step, "Final transaction summary");
    tm.displayTransactions();
}

// Scenario 3: Deadlock Detection
void demoDeadlockDetection() {
    printHeader("Scenario 3: Deadlock Detection via Wait-For Graph");

    cout << "\nA deadlock occurs when two or more transactions form a cycle" << endl;
    cout << "of waits. We detect this by building a wait-for graph and" << endl;
    cout << "running DFS cycle detection.\n" << endl;

    TransactionManager tm;
    int step = 0;

    printStep(++step, "Setup: T1 locks A, T2 locks B");
    int t1 = tm.begin();
    int t2 = tm.begin();
    tm.write(t1, "A", 10);   // T1 holds X-lock on A
    tm.write(t2, "B", 20);   // T2 holds X-lock on B
    tm.displayLockTable();

    printStep(++step, "T1 tries to write B → BLOCKED (T2 holds X-lock on B)");
    tm.write(t1, "B", 15);   // T1 waits for T2
    tm.displayLockTable();

    printStep(++step, "T2 tries to write A → BLOCKED (T1 holds X-lock on A)");
    tm.write(t2, "A", 25);   // T2 waits for T1 → DEADLOCK!
    tm.displayLockTable();

    printStep(++step, "Run deadlock detection — build wait-for graph");
    tm.displayWaitForGraph();

    vector<int> cycle = tm.detectDeadlock();
    if (!cycle.empty()) {
        cout << "\n  🔴 DEADLOCK DETECTED! Cycle: ";
        for (size_t i = 0; i < cycle.size(); i++) {
            cout << "T" << cycle[i];
            if (i < cycle.size() - 1) cout << " → ";
        }
        cout << endl;

        // Resolution: abort the youngest transaction (highest txnId)
        int victim = *max_element(cycle.begin(), cycle.end());
        cout << "\n  Resolution: Aborting T" << victim << " (youngest in cycle)" << endl;

        printStep(++step, "Abort victim T" + to_string(victim));
        tm.abort(victim);
        tm.displayLockTable();

        printStep(++step, "T1 can now proceed — re-attempt write B");
        tm.write(t1, "B", 15);
        tm.commit(t1);
        tm.displayLockTable();
    } else {
        cout << "\n  ✓ No deadlock detected." << endl;
    }

    printStep(++step, "Final state");
    tm.displayTransactions();
    tm.displayVersionChains();
}

// Scenario 4: Combined — full transaction lifecycle
void demoCombinedScenario() {
    printHeader("Scenario 4: Combined MVCC + S2PL Transaction Lifecycle");

    TransactionManager tm;
    int step = 0;

    printStep(++step, "Initialize: T1 creates accounts A=1000, B=500");
    int t1 = tm.begin();
    tm.write(t1, "A", 1000);
    tm.write(t1, "B", 500);
    tm.commit(t1);

    printStep(++step, "T2: Transfer 200 from A to B");
    int t2 = tm.begin();
    auto [f1, valA] = tm.read(t2, "A");
    auto [f2, valB] = tm.read(t2, "B");
    cout << "  T2 reads: A=" << valA << ", B=" << valB << endl;
    tm.write(t2, "A", valA - 200);  // A = 800
    tm.write(t2, "B", valB + 200);  // B = 700

    printStep(++step, "T3 starts BEFORE T2 commits — reads stale snapshot");
    int t3 = tm.begin();

    printStep(++step, "T2 commits the transfer");
    tm.commit(t2);

    printStep(++step, "T3 reads A and B — sees pre-transfer values (snapshot isolation!)");
    tm.read(t3, "A");
    tm.read(t3, "B");
    tm.commit(t3);

    printStep(++step, "T4 starts AFTER T2 — sees updated values");
    int t4 = tm.begin();
    tm.read(t4, "A");
    tm.read(t4, "B");
    tm.commit(t4);

    printStep(++step, "Version chains show full history");
    tm.displayVersionChains();

    printStep(++step, "All transaction logs");
    tm.displayTransactions();
    for (int id = 1; id <= 4; id++) {
        tm.displayTransactionLog(id);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Part 6 — Interactive Mode
// ─────────────────────────────────────────────────────────────────────────────

void interactiveMode() {
    TransactionManager tm;

    cout << "\n━━━ Interactive Transaction Manager ━━━" << endl;
    cout << "Commands:" << endl;
    cout << "  begin              — Start a new transaction" << endl;
    cout << "  read  <txn> <key>  — Read a key in a transaction" << endl;
    cout << "  write <txn> <key> <val> — Write a value" << endl;
    cout << "  commit <txn>       — Commit a transaction" << endl;
    cout << "  abort  <txn>       — Abort a transaction" << endl;
    cout << "  deadlock           — Run deadlock detection" << endl;
    cout << "  locks              — Show lock table" << endl;
    cout << "  versions           — Show MVCC version chains" << endl;
    cout << "  status             — Show transaction status" << endl;
    cout << "  waitfor            — Show wait-for graph" << endl;
    cout << "  exit               — Quit" << endl;

    string line;
    while (true) {
        cout << "\ntxn> ";
        if (!getline(cin, line)) break;

        // Trim
        size_t a = line.find_first_not_of(" \t");
        if (a == string::npos) continue;
        line = line.substr(a);

        istringstream iss(line);
        string cmd;
        iss >> cmd;

        // Convert to lowercase
        transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if (cmd == "exit" || cmd == "quit") {
            cout << "Goodbye!" << endl;
            break;
        }
        else if (cmd == "begin") {
            int id = tm.begin();
            cout << "  Started transaction T" << id << endl;
        }
        else if (cmd == "read") {
            int txnId; string key;
            if (iss >> txnId >> key) {
                tm.read(txnId, key);
            } else {
                cout << "  Usage: read <txn_id> <key>" << endl;
            }
        }
        else if (cmd == "write") {
            int txnId, val; string key;
            if (iss >> txnId >> key >> val) {
                tm.write(txnId, key, val);
            } else {
                cout << "  Usage: write <txn_id> <key> <value>" << endl;
            }
        }
        else if (cmd == "commit") {
            int txnId;
            if (iss >> txnId) {
                tm.commit(txnId);
            } else {
                cout << "  Usage: commit <txn_id>" << endl;
            }
        }
        else if (cmd == "abort") {
            int txnId;
            if (iss >> txnId) {
                tm.abort(txnId);
            } else {
                cout << "  Usage: abort <txn_id>" << endl;
            }
        }
        else if (cmd == "deadlock") {
            tm.displayWaitForGraph();
            auto cycle = tm.detectDeadlock();
            if (!cycle.empty()) {
                cout << "  🔴 DEADLOCK: ";
                for (size_t i = 0; i < cycle.size(); i++) {
                    cout << "T" << cycle[i];
                    if (i < cycle.size() - 1) cout << " → ";
                }
                cout << endl;
            } else {
                cout << "  ✓ No deadlock." << endl;
            }
        }
        else if (cmd == "locks") {
            tm.displayLockTable();
        }
        else if (cmd == "versions") {
            tm.displayVersionChains();
        }
        else if (cmd == "status") {
            tm.displayTransactions();
        }
        else if (cmd == "waitfor") {
            tm.displayWaitForGraph();
        }
        else {
            cout << "  Unknown command: " << cmd << endl;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main()
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    cout << "=========================================================" << endl;
    cout << "  Lab 8: Transaction Manager                             " << endl;
    cout << "  MVCC Version Chains + Strict 2PL + Deadlock Detection  " << endl;
    cout << "=========================================================" << endl;

    // Run all automatic demo scenarios
    demoMVCCVersionChains();
    demoStrict2PL();
    demoDeadlockDetection();
    demoCombinedScenario();

    // Interactive mode
    cout << "\n\n=========================================================\n" << endl;
    interactiveMode();

    return 0;
}
