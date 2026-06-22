#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <stack>
#include <algorithm>

using namespace std;

// Transaction States
enum TxState { ACTIVE, COMMITTED, ABORTED, BLOCKED };

struct Transaction {
    int id;
    TxState state;
    unordered_set<string> holdingLocks; // Locks held by this transaction (Record IDs)
};

// MVCC Version structure
struct Version {
    string val;
    int xmin; // TxID that created this version
    int xmax; // TxID that deleted/superseded this version (0 if current)
    Version* next; // Pointer to older version
};

// Database Record holding a version chain
struct Record {
    string id;
    Version* versionHead;
};

// Lock Types for 2PL
enum LockMode { S_LOCK, X_LOCK };

struct LockRequest {
    int txId;
    LockMode mode;
};

struct LockEntry {
    string recordId;
    unordered_set<int> holders; // TxIDs currently holding locks
    LockMode currentMode;
    queue<LockRequest> waitQueue; // Queue of transactions waiting for a lock
};

// Global DB State
unordered_map<int, Transaction> txTable;
unordered_map<string, Record> dbRecords;
unordered_map<string, LockEntry> lockTable;
int globalTxCounter = 0;

// Initialize Database records
void createRecord(const string& id, const string& initialVal) {
    Version* v = new Version{initialVal, 0, 0, nullptr};
    dbRecords[id] = Record{id, v};
}

// Start a transaction
int beginTransaction() {
    globalTxCounter++;
    txTable[globalTxCounter] = Transaction{globalTxCounter, ACTIVE, {}};
    cout << "[TxManager] Transaction Tx" << globalTxCounter << " started.\n";
    return globalTxCounter;
}

// Deadlock Detection - Wait-For Graph (WFG)
// Returns true if a cycle is detected and aborts the youngest transaction in the cycle
bool detectAndResolveDeadlock() {
    // Adjacency list: TxID -> Set of TxIDs it is waiting for
    unordered_map<int, unordered_set<int>> wfg;
    
    // Construct WFG from lock table wait queues
    for (const auto& pair : lockTable) {
        const LockEntry& entry = pair.second;
        if (entry.waitQueue.empty()) continue;
        
        // The first transaction in waitQueue is waiting for all active holders
        int waitingTx = entry.waitQueue.front().txId;
        for (int holder : entry.holders) {
            if (waitingTx != holder) {
                wfg[waitingTx].insert(holder);
            }
        }
    }
    
    // Print wait-for graph
    if (!wfg.empty()) {
        cout << "[DeadlockDetector] Wait-For Graph: ";
        for (const auto& node : wfg) {
            cout << "Tx" << node.first << " -> { ";
            for (int dest : node.second) cout << "Tx" << dest << " ";
            cout << "} ";
        }
        cout << "\n";
    }

    // Cycle detection using DFS (recursive stack tracking)
    unordered_set<int> visited;
    unordered_set<int> recStack;
    vector<int> cyclePath;
    bool hasCycle = false;
    int cycleStartNode = -1;

    auto dfs = [&](auto& self, int u) -> bool {
        visited.insert(u);
        recStack.insert(u);
        cyclePath.push_back(u);

        for (int v : wfg[u]) {
            if (recStack.count(v)) {
                cycleStartNode = v;
                return true;
            }
            if (!visited.count(v)) {
                if (self(self, v)) return true;
            }
        }
        recStack.erase(u);
        cyclePath.pop_back();
        return false;
    };

    for (const auto& pair : wfg) {
        int startNode = pair.first;
        if (!visited.count(startNode)) {
            if (dfs(dfs, startNode)) {
                hasCycle = true;
                break;
            }
        }
    }

    if (hasCycle) {
        cout << "[DeadlockDetector] Deadlock cycle detected: ";
        vector<int> cycle;
        bool inCycle = false;
        for (int node : cyclePath) {
            if (node == cycleStartNode) inCycle = true;
            if (inCycle) {
                cycle.push_back(node);
                cout << "Tx" << node << " -> ";
            }
        }
        cout << "Tx" << cycleStartNode << "\n";

        // Youngest Transaction abort policy (highest Tx ID is the youngest)
        int victim = *max_element(cycle.begin(), cycle.end());
        cout << "[DeadlockDetector] Resolving deadlock by aborting youngest Tx" << victim << "...\n";
        
        // Mark victim aborted
        txTable[victim].state = ABORTED;
        
        // Release victim's locks
        vector<string> locksToRelease(txTable[victim].holdingLocks.begin(), txTable[victim].holdingLocks.end());
        for (const string& recId : locksToRelease) {
            lockTable[recId].holders.erase(victim);
            txTable[victim].holdingLocks.erase(recId);
            
            // If lock is empty, grant to next waiting tx if applicable
            if (lockTable[recId].holders.empty() && !lockTable[recId].waitQueue.empty()) {
                LockRequest req = lockTable[recId].waitQueue.front();
                lockTable[recId].waitQueue.pop();
                lockTable[recId].holders.insert(req.txId);
                lockTable[recId].currentMode = req.mode;
                txTable[req.txId].holdingLocks.insert(recId);
                txTable[req.txId].state = ACTIVE; // Wake up
                cout << "[LockManager] Wake up: Lock on Record " << recId << " granted to Tx" << req.txId << "\n";
            }
        }

        // Clean wait queues from aborted victim
        for (auto& pair : lockTable) {
            LockEntry& entry = pair.second;
            queue<LockRequest> tempQueue;
            while (!entry.waitQueue.empty()) {
                LockRequest req = entry.waitQueue.front();
                entry.waitQueue.pop();
                if (req.txId != victim) {
                    tempQueue.push(req);
                }
            }
            entry.waitQueue = tempQueue;
        }

        return true;
    }

    return false;
}

// Request Lock under Strict 2PL
bool requestLock(int txId, const string& recordId, LockMode mode) {
    if (txTable[txId].state == ABORTED) {
        cout << "[LockManager] Refused lock on Record " << recordId << " for aborted Tx" << txId << "\n";
        return false;
    }

    LockEntry& entry = lockTable[recordId];
    entry.recordId = recordId;

    // Case 1: No locks currently held
    if (entry.holders.empty()) {
        entry.holders.insert(txId);
        entry.currentMode = mode;
        txTable[txId].holdingLocks.insert(recordId);
        cout << "[LockManager] Granted " << (mode == S_LOCK ? "Shared" : "Exclusive") 
             << " Lock on Record " << recordId << " to Tx" << txId << "\n";
        return true;
    }

    // Case 2: Shared lock requested, and currently shared
    if (mode == S_LOCK && entry.currentMode == S_LOCK) {
        entry.holders.insert(txId);
        txTable[txId].holdingLocks.insert(recordId);
        cout << "[LockManager] Granted Shared Lock on Record " << recordId << " to Tx" << txId << " (Shared-Lock Share)\n";
        return true;
    }

    // Case 3: Lock upgrade (already holds lock and wants to upgrade to Exclusive)
    if (mode == X_LOCK && entry.holders.size() == 1 && entry.holders.count(txId)) {
        entry.currentMode = X_LOCK;
        cout << "[LockManager] Upgraded Lock to Exclusive on Record " << recordId << " for Tx" << txId << "\n";
        return true;
    }

    // Case 4: Conflict, Transaction must wait
    cout << "[LockManager] Conflict: Tx" << txId << " blocked waiting for " 
         << (mode == S_LOCK ? "Shared" : "Exclusive") << " Lock on Record " << recordId << "\n";
    txTable[txId].state = BLOCKED;
    entry.waitQueue.push(LockRequest{txId, mode});
    
    // Check for deadlocks immediately when blocked
    while (detectAndResolveDeadlock());

    return false;
}

// Read Record under MVCC version chain (Snapshot read visibility logic)
void readRecord(int txId, const string& recordId) {
    if (txTable[txId].state == ABORTED) return;
    
    // Acquire Shared Lock
    if (txTable[txId].state == ACTIVE) {
        if (!requestLock(txId, recordId, S_LOCK)) {
            return;
        }
    }

    // Read matching snapshot version
    Record& rec = dbRecords[recordId];
    Version* current = rec.versionHead;
    
    while (current != nullptr) {
        // Read rules:
        // 1. Version is visible if created by a transaction committed before us or created by us.
        // 2. Version is NOT visible if deleted/superseded by a committed transaction.
        bool createdByUs = (current->xmin == txId);
        bool createdBeforeUs = (current->xmin < txId); // Simple timestamp logic
        
        bool deletedByUs = (current->xmax == txId);
        bool deletedBeforeUs = (current->xmax != 0 && current->xmax < txId);

        if ((createdByUs || createdBeforeUs) && !(deletedByUs || deletedBeforeUs)) {
            cout << "[Read] Tx" << txId << " reads Record " << recordId << " = \"" << current->val << "\"\n";
            return;
        }
        current = current->next;
    }
    cout << "[Read] Tx" << txId << " reads Record " << recordId << " = NULL (Not Visible)\n";
}

// Write Record under MVCC (Creates a new version on the chain)
void writeRecord(int txId, const string& recordId, const string& newVal) {
    if (txTable[txId].state == ABORTED) return;

    // Acquire Exclusive Lock
    if (txTable[txId].state == ACTIVE) {
        if (!requestLock(txId, recordId, X_LOCK)) {
            return;
        }
    }

    // MVCC Version Chain insertion
    Record& rec = dbRecords[recordId];
    Version* oldHead = rec.versionHead;

    // Create new version and insert at head of chain
    Version* newVersion = new Version{newVal, txId, 0, oldHead};
    rec.versionHead = newVersion;

    // Mark previous head as superseded by current transaction ID
    if (oldHead != nullptr) {
        oldHead->xmax = txId;
    }

    cout << "[Write] Tx" << txId << " wrote Record " << recordId << " = \"" << newVal << "\"\n";
}

// Commit Transaction (Releases all locks)
void commitTransaction(int txId) {
    if (txTable[txId].state != ACTIVE) return;

    txTable[txId].state = COMMITTED;
    cout << "[TxManager] Tx" << txId << " committed successfully.\n";

    // Release all locks
    vector<string> locksToRelease(txTable[txId].holdingLocks.begin(), txTable[txId].holdingLocks.end());
    for (const string& recId : locksToRelease) {
        lockTable[recId].holders.erase(txId);
        txTable[txId].holdingLocks.erase(recId);

        // Grant to next waiting transaction if lock is free
        if (lockTable[recId].holders.empty() && !lockTable[recId].waitQueue.empty()) {
            LockRequest req = lockTable[recId].waitQueue.front();
            lockTable[recId].waitQueue.pop();
            lockTable[recId].holders.insert(req.txId);
            lockTable[recId].currentMode = req.mode;
            txTable[req.txId].holdingLocks.insert(recId);
            txTable[req.txId].state = ACTIVE; // Wake up
            cout << "[LockManager] Wake up: Lock on Record " << recId << " granted to Tx" << req.txId << "\n";
        }
    }
}

int main() {
    // Initialize database items
    createRecord("A", "Apple");
    createRecord("B", "Banana");

    cout << "=== Scenario 1: Normal Concurrency (Strict 2PL & MVCC) ===\n";
    int tx1 = beginTransaction();
    readRecord(tx1, "A");
    writeRecord(tx1, "A", "Apricot");

    int tx2 = beginTransaction();
    // Snapshot Read under MVCC allows reading older versions without blocking
    readRecord(tx2, "A"); // Should read "Apple" because Tx1 is not committed
    
    commitTransaction(tx1);
    
    // Now Tx2 reads post-commit
    readRecord(tx2, "A"); // Should read "Apricot"
    commitTransaction(tx2);

    cout << "\n=== Scenario 2: Deadlock Simulation (Lock Contention & Cycle Detection) ===\n";
    int tx3 = beginTransaction();
    int tx4 = beginTransaction();

    // Tx3 locks Record A
    writeRecord(tx3, "A", "Avocado");

    // Tx4 locks Record B
    writeRecord(tx4, "B", "Blueberry");

    // Tx3 requests Lock on Record B -> Blocks waiting for Tx4
    cout << "\nTx3 requests lock on Record B...\n";
    writeRecord(tx3, "B", "Blackberry");

    // Tx4 requests Lock on Record A -> Blocks waiting for Tx3. Deadlock Cycle!
    cout << "\nTx4 requests lock on Record A...\n";
    writeRecord(tx4, "A", "Almond");

    // After deadlock resolution, check final status of Tx3
    cout << "\n=== Post-Deadlock Execution ===\n";
    if (txTable[tx3].state == ACTIVE) {
        // Since Tx4 was aborted, Tx3 should have woken up and completed its write
        writeRecord(tx3, "B", "Blackberry");
        commitTransaction(tx3);
    }

    return 0;
}
