// Lab 8 - Transaction Manager: MVCC + Strict 2PL + deadlock detection
// Akshat Kushwaha | 24BCS10060
//
// A single-threaded, in-memory transaction manager that puts together the three
// concurrency-control ideas from the lab:
//
//   * MVCC (multi-version concurrency control) for reads. Each key keeps a chain
//     of versions tagged with (begin_ts, end_ts). A transaction reads from the
//     snapshot it took at begin(), so readers never block and always see a
//     consistent view, even if someone commits a newer value meanwhile.
//
//   * Strict Two-Phase Locking for writes. A writer takes an exclusive lock on
//     each key it changes and holds every lock until commit/abort. If the lock
//     is busy the write is told to wait (returns Blocked).
//
//   * Deadlock detection via a waits-for graph. When a write blocks we add an
//     edge "waiter -> holder" and look for a cycle. If there is one, the
//     youngest transaction in the cycle (highest id) is aborted to break it.
//
// I kept everything single-threaded and deterministic so the demo prints the
// same thing every run and the logic is easy to follow.
//
// Build: g++ -std=c++17 -Wall -Wextra txn_manager.cpp -o txn_manager
// Run:   ./txn_manager

#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class Result { Ok, Blocked, Aborted, NotFound };
enum class TxnState { Active, Committed, Aborted };

class TxnManager {
public:
    // Start a transaction. Its snapshot is "everything committed so far".
    int begin() {
        int id = ++last_id_;
        Txn t;
        t.snapshot = commit_clock_;
        t.state    = TxnState::Active;
        txns_[id]  = std::move(t);
        return id;
    }

    // MVCC read: walk the version chain newest-first and return the first
    // version visible to this transaction's snapshot (or its own pending write).
    Result read(int id, const std::string& key, std::string& out) {
        Txn& t = txns_.at(id);
        if (t.state != TxnState::Active) return Result::Aborted;

        auto pend = t.pending.find(key);
        if (pend != t.pending.end()) { out = pend->second; return Result::Ok; }

        auto it = store_.find(key);
        if (it == store_.end()) return Result::NotFound;
        for (auto v = it->second.rbegin(); v != it->second.rend(); ++v) {
            if (v->begin_ts <= t.snapshot && (v->end_ts == 0 || v->end_ts > t.snapshot)) {
                out = v->value;
                return Result::Ok;
            }
        }
        return Result::NotFound;
    }

    // Strict 2PL write: grab the exclusive lock (or wait), then buffer the value
    // until commit. Returns Aborted if this txn became a deadlock victim.
    Result write(int id, const std::string& key, const std::string& value) {
        Txn& t = txns_.at(id);
        if (t.state != TxnState::Active) return Result::Aborted;

        // already hold the lock -> just update our buffered value
        if (t.locks.count(key)) { t.pending[key] = value; return Result::Ok; }

        auto held = lock_owner_.find(key);
        if (held != lock_owner_.end() && held->second != id) {
            waits_for_[id] = held->second;                 // id waits on the holder
            std::optional<int> victim = find_deadlock_victim(id);
            if (victim) {
                std::cout << "    [deadlock] cycle found, aborting youngest txn T"
                          << *victim << "\n";
                abort(*victim);
                if (*victim == id) return Result::Aborted;
            }
            return Result::Blocked;                         // caller should retry later
        }

        // lock is free -> take it
        lock_owner_[key] = id;
        t.locks.insert(key);
        t.pending[key] = value;
        waits_for_.erase(id);
        return Result::Ok;
    }

    Result commit(int id) {
        Txn& t = txns_.at(id);
        if (t.state != TxnState::Active) return Result::Aborted;

        ++commit_clock_;
        for (auto& [key, value] : t.pending) {
            auto& chain = store_[key];
            if (!chain.empty() && chain.back().end_ts == 0)
                chain.back().end_ts = commit_clock_;        // close the old version
            chain.push_back(Version{value, commit_clock_, 0});
        }
        release(t);
        t.state = TxnState::Committed;
        return Result::Ok;
    }

    void abort(int id) {
        auto it = txns_.find(id);
        if (it == txns_.end() || it->second.state != TxnState::Active) return;
        release(it->second);
        it->second.pending.clear();
        it->second.state = TxnState::Aborted;
    }

    TxnState state(int id) const {
        auto it = txns_.find(id);
        return it == txns_.end() ? TxnState::Aborted : it->second.state;
    }

private:
    struct Version {
        std::string value;
        long        begin_ts;   // commit time this version became visible
        long        end_ts;     // commit time it was replaced (0 = still live)
    };
    struct Txn {
        long                            snapshot = 0;
        TxnState                        state = TxnState::Active;
        std::map<std::string,std::string> pending;   // buffered (uncommitted) writes
        std::unordered_set<std::string>   locks;     // X-locks currently held
    };

    void release(Txn& t) {
        for (const std::string& k : t.locks) lock_owner_.erase(k);
        t.locks.clear();
    }

    // Follow waits_for_ edges from `start`. If we come back to `start` there is
    // a cycle; the victim is the highest txn id on it (the youngest).
    std::optional<int> find_deadlock_victim(int start) {
        std::unordered_set<int> seen;
        std::vector<int> cycle;
        int cur = start;
        while (true) {
            auto it = waits_for_.find(cur);
            if (it == waits_for_.end()) return std::nullopt;   // chain ends, no cycle
            int next = it->second;
            cycle.push_back(cur);
            if (next == start) {                               // closed the loop
                int victim = start;
                for (int x : cycle) if (x > victim) victim = x;
                return victim;
            }
            if (seen.count(next)) return std::nullopt;         // cycle not through start
            seen.insert(next);
            cur = next;
        }
    }

    int  last_id_ = 0;
    long commit_clock_ = 0;
    std::unordered_map<std::string, std::vector<Version>> store_;       // key -> versions
    std::unordered_map<std::string, int>                  lock_owner_;  // key -> txn holding X-lock
    std::unordered_map<int, int>                          waits_for_;   // waiter -> holder
    std::unordered_map<int, Txn>                          txns_;
};

// --------------------------- demo ------------------------------------------
namespace {
std::string read_val(TxnManager& m, int t, const std::string& key) {
    std::string v;
    return m.read(t, key, v) == Result::Ok ? v : std::string("<none>");
}
void line(const std::string& s) { std::cout << "\n=== " << s << " ===\n"; }
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  [pass] " : "  [FAIL] ") << what << "\n";
}
}  // namespace

int main() {
    std::cout << "Transaction Manager demo | Akshat Kushwaha | 24BCS10060\n";
    TxnManager m;

    line("seed: alice=100, bob=50");
    {
        int s = m.begin();
        m.write(s, "alice", "100");
        m.write(s, "bob", "50");
        m.commit(s);
    }

    line("1) MVCC snapshot isolation");
    {
        int reader = m.begin();                         // snapshot taken now
        check(read_val(m, reader, "alice") == "100", "reader sees alice=100");

        int writer = m.begin();
        m.write(writer, "alice", "175");
        m.commit(writer);                               // alice is now 175

        check(read_val(m, reader, "alice") == "100", "old reader STILL sees alice=100");
        int fresh = m.begin();
        check(read_val(m, fresh, "alice") == "175", "fresh reader sees alice=175");
    }

    line("2) Strict 2PL - second writer blocks");
    {
        int t1 = m.begin();
        int t2 = m.begin();
        check(m.write(t1, "bob", "60") == Result::Ok,      "T1 locks bob");
        check(m.write(t2, "bob", "70") == Result::Blocked, "T2 is blocked on bob");
        m.abort(t1);                                       // T1 releases the lock
        check(m.write(t2, "bob", "70") == Result::Ok,      "T2 gets the lock after T1 aborts");
        m.commit(t2);
        int v = m.begin();
        check(read_val(m, v, "bob") == "70", "bob is now 70");
    }

    line("3) Deadlock detection - youngest aborted");
    {
        int ta = m.begin();                                // older
        int tb = m.begin();                                // younger
        check(m.write(ta, "alice", "1") == Result::Ok, "T_a locks alice");
        check(m.write(tb, "bob", "2")   == Result::Ok, "T_b locks bob");
        // now they cross: each wants the lock the other holds
        check(m.write(ta, "bob", "3")   == Result::Blocked, "T_a waits for bob (held by T_b)");
        Result r = m.write(tb, "alice", "4");              // closes the cycle
        check(m.state(tb) == TxnState::Aborted, "T_b (youngest) chosen as deadlock victim");
        (void)r;
        check(m.write(ta, "bob", "3") == Result::Ok, "T_a can now take bob and finish");
        m.commit(ta);
    }

    std::cout << "\nAll scenarios ran.\n";
    return 0;
}
