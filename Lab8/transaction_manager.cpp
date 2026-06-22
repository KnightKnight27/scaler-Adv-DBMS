// Lab 8 - Simplified Transaction Manager
// Author: 24BCS10345 Ansh Mahajan
//
// A single-threaded simulation of the core concurrency-control machinery a
// real database engine uses, wired together so the four mechanisms interact:
//
//   1. MVCC version chains   - every write appends a new Version to a key's
//                              chain instead of overwriting. A read sees its
//                              own uncommitted writes, otherwise the latest
//                              COMMITTED version.
//   2. Strict 2PL            - a read needs a Shared lock, a write an Exclusive
//                              lock; all locks are held until commit/abort.
//                              This is what guarantees serializability.
//   3. Deadlock detection    - blocked requests add edges to a Wait-For Graph;
//                              if a request would close a cycle, that requester
//                              is chosen as the victim and aborted.
//   4. Lifecycle             - begin -> (read/write)* -> commit | abort, with
//                              commit publishing versions and abort rolling
//                              them back; releasing locks wakes waiters.
//
// Because the model is single-threaded, a blocked operation is "parked" with
// its pending action and automatically resumed when the lock it needs is freed.
//
// Build: g++ -std=c++17 transaction_manager.cpp -o txn
// Run:   ./txn

#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

// ----------------------------- MVCC store -----------------------------------
struct Version {
    int value;
    int writer;        // transaction id that created this version
    bool committed;
};

enum class LockMode { Shared, Exclusive };

const char* modeName(LockMode m) {
    return m == LockMode::Shared ? "S" : "X";
}

// A parked operation, replayed when the transaction's lock is finally granted.
struct PendingOp {
    enum class Kind { Read, Write } kind;
    std::string key;
    int value = 0;     // for Write
};

struct Transaction {
    int id = 0;
    enum class State { Active, Waiting, Committed, Aborted } state = State::Active;
    std::set<std::string> locks;     // keys this transaction currently holds
    bool hasPending = false;
    PendingOp pending;
};

// --------------------------- the lock table ----------------------------------
struct LockEntry {
    LockMode mode = LockMode::Shared;
    std::set<int> holders;
    struct Request { int txn; LockMode mode; };
    std::deque<Request> queue;
};

class TransactionManager {
public:
    int begin() {
        int id = nextId_++;
        Transaction t;
        t.id = id;
        txns_[id] = t;
        std::cout << "T" << id << ": BEGIN\n";
        return id;
    }

    // Returns true if the read completed now, false if it was parked/aborted.
    bool read(int id, const std::string& key) {
        Transaction& t = txns_[id];
        std::cout << "T" << id << ": READ " << key << " -> ";
        Grant g = acquire(id, key, LockMode::Shared);
        if (g == Grant::Aborted) {
            std::cout << "chosen as victim\n";
            abort(id);            // roll back + release locks, waking waiters
            return false;
        }
        if (g == Grant::Blocked) {
            park(t, {PendingOp::Kind::Read, key, 0});
            std::cout << "blocked (waiting for lock)\n";
            return false;
        }
        std::cout << showVisible(id, key) << "\n";
        return true;
    }

    bool write(int id, const std::string& key, int value) {
        Transaction& t = txns_[id];
        std::cout << "T" << id << ": WRITE " << key << " = " << value << " -> ";
        Grant g = acquire(id, key, LockMode::Exclusive);
        if (g == Grant::Aborted) {
            std::cout << "chosen as victim\n";
            abort(id);            // roll back + release locks, waking waiters
            return false;
        }
        if (g == Grant::Blocked) {
            park(t, {PendingOp::Kind::Write, key, value});
            std::cout << "blocked (waiting for lock)\n";
            return false;
        }
        applyWrite(id, key, value);
        std::cout << "ok (new version)\n";
        return true;
    }

    void commit(int id) {
        Transaction& t = txns_[id];
        for (auto& [key, chain] : store_) {
            for (Version& v : chain) {
                if (v.writer == id) {
                    v.committed = true;     // publish this transaction's writes
                }
            }
        }
        t.state = Transaction::State::Committed;
        std::cout << "T" << id << ": COMMIT (versions published)\n";
        releaseAll(id);
    }

    void abort(int id) {
        Transaction& t = txns_[id];
        for (auto& [key, chain] : store_) {
            chain.erase(std::remove_if(chain.begin(), chain.end(),
                                       [&](const Version& v) {
                                           return v.writer == id;
                                       }),
                        chain.end());        // roll back uncommitted writes
        }
        t.state = Transaction::State::Aborted;
        t.hasPending = false;
        std::cout << "T" << id << ": ABORT (writes rolled back)\n";
        releaseAll(id);
    }

    void dumpChain(const std::string& key) const {
        std::cout << "version chain of " << key << ": ";
        auto it = store_.find(key);
        if (it == store_.end() || it->second.empty()) {
            std::cout << "(none)\n";
            return;
        }
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            const Version& v = it->second[i];
            std::cout << "[v" << (i + 1) << "=" << v.value << " byT" << v.writer
                      << (v.committed ? " committed" : " uncommitted") << "]"
                      << (i + 1 < it->second.size() ? " -> " : "");
        }
        std::cout << "\n";
    }

private:
    enum class Grant { Granted, Blocked, Aborted };

    int nextId_ = 1;
    std::map<int, Transaction> txns_;
    std::map<std::string, std::vector<Version>> store_;   // MVCC chains
    std::map<std::string, LockEntry> locks_;
    std::map<int, std::set<int>> waitsFor_;               // wait-for graph

    // --- MVCC visibility ---
    int latestVisible(int id, const std::string& key, bool& found) const {
        found = false;
        auto it = store_.find(key);
        if (it == store_.end()) {
            return 0;
        }
        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
            if (rit->writer == id || rit->committed) {
                found = true;
                return rit->value;
            }
        }
        return 0;
    }

    std::string showVisible(int id, const std::string& key) const {
        bool found = false;
        int v = latestVisible(id, key, found);
        return found ? std::to_string(v) : std::string("NULL");
    }

    void applyWrite(int id, const std::string& key, int value) {
        store_[key].push_back(Version{value, id, false});
        txns_[id].locks.insert(key);
    }

    // --- locking ---
    Grant acquire(int id, const std::string& key, LockMode mode) {
        LockEntry& e = locks_[key];

        if (e.holders.empty()) {                 // free lock
            e.holders.insert(id);
            e.mode = mode;
            txns_[id].locks.insert(key);
            return Grant::Granted;
        }
        if (e.holders.size() == 1 && *e.holders.begin() == id) {
            if (mode == LockMode::Exclusive) {   // sole holder may upgrade
                e.mode = LockMode::Exclusive;
            }
            return Grant::Granted;
        }
        if (e.holders.count(id)) {               // already share with others
            if (mode == LockMode::Shared && e.mode == LockMode::Shared) {
                return Grant::Granted;
            }
            // wants X while others hold S -> must wait on the others
        }

        // A new Shared request may join only if the lock is currently held in
        // Shared mode and no earlier request (e.g. a writer) is already queued.
        bool compatible = (mode == LockMode::Shared &&
                           e.mode == LockMode::Shared && e.queue.empty());
        if (compatible) {
            e.holders.insert(id);
            txns_[id].locks.insert(key);
            return Grant::Granted;
        }

        // Block: queue the request and record wait-for edges to every holder.
        e.queue.push_back({id, mode});
        txns_[id].state = Transaction::State::Waiting;
        for (int holder : e.holders) {
            if (holder != id) {
                waitsFor_[id].insert(holder);
            }
        }
        if (hasCycle(id)) {
            // This request closes a cycle: pick the requester as the victim.
            std::cout << "DEADLOCK detected (cycle in wait-for graph); ";
            removeFromQueue(key, id);
            clearEdgesFrom(id);
            return Grant::Aborted;
        }
        return Grant::Blocked;
    }

    void park(Transaction& t, const PendingOp& op) {
        t.hasPending = true;
        t.pending = op;
    }

    void releaseAll(int id) {
        // Drop this transaction from every lock it holds or waits on.
        std::set<std::string> touched;
        for (auto& [key, e] : locks_) {
            if (e.holders.erase(id)) {
                touched.insert(key);
            }
            removeFromQueue(key, id);
        }
        clearEdgesFrom(id);
        clearEdgesTo(id);
        txns_[id].locks.clear();

        // Wake whatever can now proceed on the freed keys.
        for (const std::string& key : touched) {
            drainQueue(key);
        }
    }

    // Grant as many queued requests on `key` as the current holders allow,
    // replaying each woken transaction's parked operation.
    void drainQueue(const std::string& key) {
        LockEntry& e = locks_[key];
        bool progress = true;
        while (progress && !e.queue.empty()) {
            progress = false;
            LockEntry::Request req = e.queue.front();
            bool canGrant = false;
            if (e.holders.empty()) {
                canGrant = true;
            } else if (req.mode == LockMode::Shared && e.mode == LockMode::Shared) {
                canGrant = true;
            }
            if (!canGrant) {
                break;                            // head of queue still blocked
            }
            e.queue.pop_front();
            e.holders.insert(req.txn);
            e.mode = req.mode;
            clearEdgesFrom(req.txn);
            Transaction& t = txns_[req.txn];
            t.state = Transaction::State::Active;
            t.locks.insert(key);
            std::cout << "  -> T" << req.txn << " granted " << modeName(req.mode)
                      << " on " << key << ", resuming\n";
            resume(req.txn);
            progress = true;
        }
    }

    void resume(int id) {
        Transaction& t = txns_[id];
        if (!t.hasPending) {
            return;
        }
        PendingOp op = t.pending;
        t.hasPending = false;
        if (op.kind == PendingOp::Kind::Write) {
            applyWrite(id, op.key, op.value);
            std::cout << "     T" << id << ": WRITE " << op.key << " = "
                      << op.value << " -> ok (new version)\n";
        } else {
            std::cout << "     T" << id << ": READ " << op.key << " -> "
                      << showVisible(id, op.key) << "\n";
        }
    }

    // --- wait-for graph helpers ---
    bool hasCycle(int start) const {
        std::set<int> visiting;
        return dfs(start, start, visiting);
    }

    bool dfs(int node, int target, std::set<int>& visiting) const {
        auto it = waitsFor_.find(node);
        if (it == waitsFor_.end()) {
            return false;
        }
        for (int nxt : it->second) {
            if (nxt == target) {
                return true;
            }
            if (visiting.insert(nxt).second) {
                if (dfs(nxt, target, visiting)) {
                    return true;
                }
            }
        }
        return false;
    }

    void clearEdgesFrom(int id) { waitsFor_.erase(id); }

    void clearEdgesTo(int id) {
        for (auto& [from, targets] : waitsFor_) {
            targets.erase(id);
        }
    }

    void removeFromQueue(const std::string& key, int id) {
        LockEntry& e = locks_[key];
        e.queue.erase(std::remove_if(e.queue.begin(), e.queue.end(),
                                     [&](const LockEntry::Request& r) {
                                         return r.txn == id;
                                     }),
                      e.queue.end());
    }
};

namespace {

void banner(const std::string& title) {
    std::cout << "\n========== " << title << " ==========\n";
}

}  // namespace

int main() {
    TransactionManager tm;

    banner("Scenario 1: MVCC version chain + lifecycle");
    int t1 = tm.begin();
    tm.write(t1, "A", 10);
    tm.write(t1, "A", 20);          // second version of A
    tm.read(t1, "A");               // sees its own latest write (20)
    tm.commit(t1);
    int t2 = tm.begin();
    tm.read(t2, "A");               // sees committed value 20
    tm.commit(t2);
    tm.dumpChain("A");

    banner("Scenario 2: Strict 2PL - reader blocks on a writer until commit");
    int t3 = tm.begin();
    int t4 = tm.begin();
    tm.write(t3, "B", 99);          // T3 takes X lock on B
    tm.read(t4, "B");               // T4 wants S on B -> blocks behind T3
    tm.commit(t3);                  // releasing B wakes T4, which now reads 99
    tm.commit(t4);

    banner("Scenario 3: Deadlock detection + resolution");
    int t5 = tm.begin();
    int t6 = tm.begin();
    tm.write(t5, "X", 1);           // T5: X-lock on X
    tm.write(t6, "Y", 2);           // T6: X-lock on Y
    tm.write(t5, "Y", 3);           // T5 waits for T6  (edge T5->T6)
    tm.write(t6, "X", 4);           // T6 waits for T5  -> cycle -> T6 aborted
    tm.commit(t5);                  // T5 (resumed after T6 released Y) commits
    tm.dumpChain("X");
    tm.dumpChain("Y");

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}
