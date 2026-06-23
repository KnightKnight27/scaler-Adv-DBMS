// Lab 8 — In-memory Transaction Manager in C++17.
//
// Name : Praneeth Budati
// Roll : 24BCS10081
//
// A small single-threaded transaction engine that puts five classic
// concurrency-control ideas into one `TransactionManager`:
//
//   1. MVCC reads     — every key keeps a chain of versions tagged with
//                       the txn that created it and the txn that later
//                       superseded it. A reader keeps the first version
//                       visible to its snapshot, so readers never block
//                       writers and writers never block readers.
//   2. Strict 2PL     — Shared / eXclusive row locks are taken before a
//                       read/write and held until commit or abort, with
//                       an in-place S->X upgrade when the asker is the
//                       sole holder.
//   3. Deadlock check — every blocking acquire runs a DFS over the
//                       waits-for graph; on a cycle the youngest txn
//                       (highest id) is aborted.
//   4. Lost update    — after taking the X lock, write() re-scans the
//                       chain and aborts if a concurrent txn committed a
//                       newer version (first-updater-wins).
//   5. vacuum()       — prunes versions whose invalidation is already
//                       visible to every live snapshot (an oldest-xmin
//                       style horizon, like Postgres VACUUM).
//
// The demo is deterministic: lock conflicts surface as an explicit
// BLOCKED status rather than a real OS-level wait, which keeps the
// output reproducible while still exercising every code path.

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

enum class State { Active, Committed, Aborted };

enum class Status { Ok, Blocked, AbortedDeadlock, AbortedLostUpdate };

const char* name(Status s) {
    switch (s) {
        case Status::Ok:                return "OK";
        case Status::Blocked:           return "BLOCKED";
        case Status::AbortedDeadlock:   return "ABORTED (deadlock)";
        case Status::AbortedLostUpdate: return "ABORTED (lost update)";
    }
    return "?";
}

// One entry in a key's version chain. `invalidator == 0` means the
// version is still current (nobody has superseded it yet).
struct Version {
    int value;
    int creator;
    int invalidator;
};

struct Txn {
    int id;
    State state = State::Active;
    std::set<int> snapshot;        // txns already committed when this began
    std::set<std::string> locks;   // keys on which this txn holds a lock
};

// A row lock: a mode plus the set of txns currently holding it. Multiple
// holders are only possible in 'S' mode.
struct LockEntry {
    char mode = 'S';
    std::set<int> holders;
};

std::string join(const std::set<int>& s) {
    std::string out;
    for (int x : s) {
        if (!out.empty()) out += ", ";
        out += "T" + std::to_string(x);
    }
    return out;
}

class TransactionManager {
public:
    int begin() {
        int id = next_id_++;
        Txn t;
        t.id = id;
        t.snapshot = committed_;
        txns_[id] = t;
        log("T" + std::to_string(id) + " BEGIN  (snapshot = {" +
            join(committed_) + "})");
        return id;
    }

    // Returns the visible value, or std::nullopt if the lock could not be
    // taken or no version is visible to this txn's snapshot.
    std::optional<int> read(int tid, const std::string& key) {
        Status st = acquire(tid, key, 'S');
        if (st != Status::Ok) {
            log("T" + std::to_string(tid) + " read " + key + " -> " +
                name(st));
            return std::nullopt;
        }
        const auto& chain = store_[key];
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (visible(*it, txns_[tid])) {
                log("T" + std::to_string(tid) + " read " + key + " = " +
                    std::to_string(it->value));
                return it->value;
            }
        }
        log("T" + std::to_string(tid) + " read " + key + " = (no version)");
        return std::nullopt;
    }

    Status write(int tid, const std::string& key, int value) {
        Status st = acquire(tid, key, 'X');
        if (st != Status::Ok) {
            log("T" + std::to_string(tid) + " write " + key + " <- " +
                std::to_string(value) + " -> " + name(st));
            return st;
        }
        // First-updater-wins: abort if a concurrent txn (one that
        // committed after our snapshot) already wrote this key.
        for (const Version& v : store_[key]) {
            if (v.creator != tid && committed_.count(v.creator) &&
                !txns_[tid].snapshot.count(v.creator)) {
                abort(tid, "lost update");
                log("T" + std::to_string(tid) + " write " + key + " <- " +
                    std::to_string(value) + " -> " +
                    name(Status::AbortedLostUpdate));
                return Status::AbortedLostUpdate;
            }
        }
        // Supersede the version this txn currently sees, then append ours.
        auto& chain = store_[key];
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (visible(*it, txns_[tid])) {
                it->invalidator = tid;
                break;
            }
        }
        chain.push_back({value, tid, 0});
        log("T" + std::to_string(tid) + " write " + key + " <- " +
            std::to_string(value) + " -> OK");
        return Status::Ok;
    }

    void commit(int tid) {
        Txn& t = txns_[tid];
        if (t.state != State::Active) return;
        t.state = State::Committed;
        committed_.insert(tid);
        releaseLocks(tid);
        clearWaits(tid);
        log("T" + std::to_string(tid) + " COMMIT");
    }

    void abort(int tid, const std::string& why) {
        Txn& t = txns_[tid];
        if (t.state != State::Active) return;
        t.state = State::Aborted;
        // Undo this txn's version-chain edits everywhere.
        for (auto& [key, chain] : store_) {
            chain.erase(std::remove_if(chain.begin(), chain.end(),
                            [&](const Version& v) { return v.creator == tid; }),
                        chain.end());
            for (Version& v : chain)
                if (v.invalidator == tid) v.invalidator = 0;
        }
        releaseLocks(tid);
        clearWaits(tid);
        log("T" + std::to_string(tid) + " ABORT  (" + why + ")");
    }

    // Prune versions whose invalidation is already visible to every live
    // snapshot — nobody will ever need them again.
    void vacuum() {
        int horizon = oldestLiveSnapshotHorizon();
        int removed = 0;
        for (auto& [key, chain] : store_) {
            auto before = chain.size();
            chain.erase(std::remove_if(chain.begin(), chain.end(),
                            [&](const Version& v) {
                                return v.invalidator != 0 &&
                                       committed_.count(v.invalidator) &&
                                       v.invalidator < horizon;
                            }),
                        chain.end());
            removed += static_cast<int>(before - chain.size());
        }
        log("VACUUM (horizon = T" + std::to_string(horizon) + ") removed " +
            std::to_string(removed) + " dead version(s)");
    }

private:
    bool visible(const Version& v, const Txn& t) const {
        bool creatorVisible = v.creator == t.id || t.snapshot.count(v.creator);
        if (!creatorVisible) return false;
        bool invVisible = v.invalidator != 0 &&
                          (v.invalidator == t.id ||
                           t.snapshot.count(v.invalidator));
        return !invVisible;
    }

    // Strict-2PL acquire with deadlock detection. Returns Ok, Blocked, or
    // AbortedDeadlock (the latter only when *this* txn is the victim).
    Status acquire(int tid, const std::string& key, char mode) {
        LockEntry& e = locks_[key];

        if (e.holders.count(tid)) {
            if (mode == 'X' && e.mode == 'S' && e.holders.size() == 1) {
                e.mode = 'X';
                log("T" + std::to_string(tid) + " upgrade S->X on " + key);
                return Status::Ok;
            }
            if (!(mode == 'X' && e.mode == 'S'))
                return Status::Ok;  // already hold a sufficient lock
        }

        std::set<int> others;
        for (int h : e.holders)
            if (h != tid) others.insert(h);

        bool conflict;
        if (e.holders.empty())
            conflict = false;
        else if (mode == 'S' && e.mode == 'S')
            conflict = false;  // shared readers coexist
        else
            conflict = !others.empty();

        if (!conflict) {
            if (e.holders.empty()) e.mode = mode;
            else if (mode == 'X') e.mode = 'X';
            e.holders.insert(tid);
            txns_[tid].locks.insert(key);
            waits_for_.erase(tid);
            return Status::Ok;
        }

        // Blocked: record waits-for edges and look for a cycle.
        waits_for_[tid] = others;
        std::vector<int> cycle;
        if (findCycle(tid, cycle)) {
            int victim = *std::max_element(cycle.begin(), cycle.end());
            log("deadlock: cycle {" + joinVec(cycle) + "} -> abort youngest T" +
                std::to_string(victim));
            abort(victim, "deadlock victim");
            if (victim == tid) return Status::AbortedDeadlock;
            return acquire(tid, key, mode);  // victim freed the lock; retry
        }
        return Status::Blocked;
    }

    // DFS that returns true iff `start` lies on a waits-for cycle, filling
    // `out` with the nodes on the path back to `start`.
    bool findCycle(int start, std::vector<int>& out) {
        std::set<int> seen;
        std::vector<int> path;
        std::function<bool(int)> dfs = [&](int u) -> bool {
            path.push_back(u);
            seen.insert(u);
            for (int v : waits_for_[u]) {
                if (v == start) { out = path; return true; }
                if (!seen.count(v) && dfs(v)) return true;
            }
            path.pop_back();
            return false;
        };
        return dfs(start);
    }

    void releaseLocks(int tid) {
        for (const std::string& key : txns_[tid].locks) {
            LockEntry& e = locks_[key];
            e.holders.erase(tid);
            if (e.holders.empty()) locks_.erase(key);
        }
        txns_[tid].locks.clear();
    }

    void clearWaits(int tid) {
        waits_for_.erase(tid);
        for (auto& [u, s] : waits_for_) s.erase(tid);
    }

    int oldestLiveSnapshotHorizon() const {
        int horizon = next_id_;  // nothing live -> prune everything committed
        for (const auto& [id, t] : txns_)
            if (t.state == State::Active) horizon = std::min(horizon, id);
        return horizon;
    }

    std::string joinVec(const std::vector<int>& v) const {
        std::string out;
        for (int x : v) {
            if (!out.empty()) out += " -> ";
            out += "T" + std::to_string(x);
        }
        return out;
    }

    void log(const std::string& msg) const { std::cout << "  " << msg << "\n"; }

    std::map<std::string, std::vector<Version>> store_;
    std::map<std::string, LockEntry> locks_;
    std::map<int, Txn> txns_;
    std::map<int, std::set<int>> waits_for_;
    std::set<int> committed_;
    int next_id_ = 1;
};

void header(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

}  // namespace

int main() {
    std::cout << "Lab 8 - in-memory transaction manager "
             "(Praneeth Budati, 24BCS10081)\n";

    TransactionManager tm;

    // Seed initial committed state with a bootstrap txn.
    int boot = tm.begin();
    tm.write(boot, "x", 100);
    tm.write(boot, "y", 200);
    tm.write(boot, "z", 300);
    tm.commit(boot);

    header("1. Snapshot isolation (MVCC: reader keeps its snapshot)");
    int r = tm.begin();           // snapshot taken before w commits
    int w = tm.begin();
    tm.write(w, "x", 111);        // w supersedes x with a new version
    tm.commit(w);                 // ...and commits it (X lock released)
    tm.read(r, "x");              // still 100 — w is outside r's snapshot
    tm.commit(r);

    header("2. Shared locks (two readers share an S lock)");
    int a = tm.begin();
    int b = tm.begin();
    tm.read(a, "y");
    tm.read(b, "y");              // both hold S on y, no conflict
    tm.commit(a);
    tm.commit(b);

    header("3. Writer blocked by a reader's S lock, then unblocked");
    int rd = tm.begin();
    int wr = tm.begin();
    tm.read(rd, "z");             // rd holds S on z
    tm.write(wr, "z", 333);       // BLOCKED — X conflicts with S
    tm.commit(rd);                // rd releases S
    tm.write(wr, "z", 333);       // retry now succeeds
    tm.commit(wr);

    header("4. Lock upgrade S -> X (sole holder)");
    int up = tm.begin();
    tm.read(up, "x");             // S lock
    tm.write(up, "x", 999);       // upgrades in place to X
    tm.commit(up);

    header("5. Deadlock detection (youngest aborted)");
    int t1 = tm.begin();
    int t2 = tm.begin();
    tm.write(t1, "x", 1);         // t1 holds X on x
    tm.write(t2, "y", 2);         // t2 holds X on y
    tm.write(t1, "y", 1);         // t1 waits for t2  (BLOCKED)
    tm.write(t2, "x", 2);         // cycle -> youngest (t2) aborted
    tm.write(t1, "y", 1);         // t1 now proceeds
    tm.commit(t1);

    header("6. Lost update (first-updater-wins)");
    int p = tm.begin();           // both snapshot the same state
    int q = tm.begin();
    tm.write(p, "y", 10);
    tm.commit(p);                 // p commits first
    tm.write(q, "y", 20);         // q wrote against a stale snapshot -> abort

    header("7. Vacuum (prune dead versions)");
    tm.vacuum();

    std::cout << "\nDone.\n";
    return 0;
}