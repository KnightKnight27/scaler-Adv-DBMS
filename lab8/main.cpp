#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

// ─────────────────────────────────────────────
//  Transaction ID
// ─────────────────────────────────────────────
static std::atomic<uint64_t> g_xid{1};
static uint64_t newXid() { return g_xid++; }

// ─────────────────────────────────────────────
//  MVCC Heap
//  Each write appends a new version tagged with
//  xmin (creator) and xmax (deleter, 0=live).
//  Readers do NOT take locks — snapshot xid
//  alone determines visibility.
// ─────────────────────────────────────────────
struct Version {
    uint64_t    xmin, xmax;   // xmax=0 → still live
    std::string value;
};

struct HeapRecord {
    std::vector<Version> versions;

    void insert(uint64_t xid, const std::string& val) {
        versions.push_back({xid, 0, val});
    }

    void update(uint64_t xid, const std::string& val,
                const std::unordered_set<uint64_t>& committed) {
        for (auto& v : versions)
            if (v.xmax == 0 && committed.count(v.xmin))
                v.xmax = xid;
        versions.push_back({xid, 0, val});
    }

    // Snapshot read: latest version where xmin committed before snap
    // AND xmax is either uncommitted or committed after snap
    std::string read(uint64_t snap,
                     const std::unordered_set<uint64_t>& committed) const {
        std::string res = "(none)";
        for (const auto& v : versions) {
            bool created = committed.count(v.xmin) && v.xmin < snap;
            bool alive   = (v.xmax == 0)
                        || !committed.count(v.xmax)
                        || (v.xmax > snap);
            if (created && alive) res = v.value;
        }
        return res;
    }
};

// ─────────────────────────────────────────────
//  Lock Manager  (Strict 2PL — writes only)
//  Exclusive locks held until commit/abort.
//  Reads are lock-free (MVCC snapshot).
//  std::map used so references stay valid across
//  new insertions while a thread is in cv.wait.
// ─────────────────────────────────────────────
struct LockEntry {
    std::unordered_set<uint64_t> holders;
    std::vector<uint64_t>        waiters;
};

class LockManager {
    std::mutex              mtx;
    std::condition_variable cv;
    std::map<std::string, LockEntry>                 table;
    std::map<uint64_t, std::unordered_set<uint64_t>> waitsFor;

    bool canGrant(const LockEntry& e, uint64_t xid) const {
        return e.holders.empty() ||
               (e.holders.size() == 1 && e.holders.count(xid));
    }

    bool hasCycle(uint64_t start, uint64_t cur,
                  std::unordered_set<uint64_t>& visited) {
        if (cur == start && !visited.empty()) return true;
        if (visited.count(cur)) return false;
        visited.insert(cur);
        auto it = waitsFor.find(cur);
        if (it == waitsFor.end()) return false;
        for (uint64_t nxt : it->second)
            if (hasCycle(start, nxt, visited)) return true;
        return false;
    }

public:
    // Acquire exclusive lock; returns false on deadlock
    bool lock(uint64_t xid, const std::string& res) {
        std::unique_lock<std::mutex> lk(mtx);
        LockEntry& e = table[res];

        if (canGrant(e, xid)) { e.holders.insert(xid); return true; }

        for (uint64_t hid : e.holders)
            if (hid != xid) waitsFor[xid].insert(hid);

        std::unordered_set<uint64_t> visited;
        if (hasCycle(xid, xid, visited)) {
            waitsFor.erase(xid);
            std::cout << "  [DEADLOCK] xid=" << xid
                      << " waiting on '" << res << "' -- aborting\n";
            return false;
        }

        e.waiters.push_back(xid);
        cv.wait(lk, [&](){ return canGrant(e, xid); });

        waitsFor.erase(xid);
        e.waiters.erase(std::remove(e.waiters.begin(), e.waiters.end(), xid),
                        e.waiters.end());
        e.holders.insert(xid);
        return true;
    }

    void releaseAll(uint64_t xid) {
        std::lock_guard<std::mutex> lk(mtx);
        for (auto& [res, e] : table) e.holders.erase(xid);
        waitsFor.erase(xid);
        cv.notify_all();
    }
};

// ─────────────────────────────────────────────
//  Database
// ─────────────────────────────────────────────
class Database {
    std::mutex                                 dbMtx;
    std::unordered_map<std::string,HeapRecord> heap;
    std::unordered_set<uint64_t>               committed;
    LockManager                                locks;

public:
    void bootstrap(const std::string& key, const std::string& val) {
        heap[key].insert(0, val);
        committed.insert(0);
    }

    uint64_t begin() {
        uint64_t xid = newXid();
        std::cout << "  [BEGIN]  xid=" << xid << "\n";
        return xid;
    }

    // Lock-free snapshot read (MVCC)
    std::string read(uint64_t xid, const std::string& key) {
        std::lock_guard<std::mutex> lk(dbMtx);
        auto it = heap.find(key);
        if (it == heap.end()) return "(not found)";
        return it->second.read(xid, committed);
    }

    // Exclusive lock + append new version (Strict 2PL)
    bool write(uint64_t xid, const std::string& key, const std::string& val) {
        if (!locks.lock(xid, key)) return false;
        std::lock_guard<std::mutex> lk(dbMtx);
        heap[key].update(xid, val, committed);
        return true;
    }

    void commit(uint64_t xid) {
        { std::lock_guard<std::mutex> lk(dbMtx); committed.insert(xid); }
        locks.releaseAll(xid);
        std::cout << "  [COMMIT] xid=" << xid << "\n";
    }

    void abort(uint64_t xid) {
        locks.releaseAll(xid);
        std::cout << "  [ABORT]  xid=" << xid << "\n";
    }

    std::string latest(const std::string& key) {
        std::lock_guard<std::mutex> lk(dbMtx);
        auto it = heap.find(key);
        if (it == heap.end()) return "(not found)";
        return it->second.read(UINT64_MAX, committed);
    }
};

// ─────────────────────────────────────────────
//  Scenario 1: MVCC Snapshot Isolation
// ─────────────────────────────────────────────
void scenario1() {
    std::cout << "\n-- Scenario 1: MVCC Snapshot Isolation --\n";
    Database db;
    db.bootstrap("balance", "1000");

    uint64_t t1 = db.begin();
    std::cout << "  T1 reads balance = " << db.read(t1, "balance") << "\n";

    uint64_t t2 = db.begin();
    db.write(t2, "balance", "1500");
    db.commit(t2);
    std::cout << "  T2 wrote balance=1500 and committed\n";

    std::string snap = db.read(t1, "balance");
    std::cout << "  T1 reads balance again = " << snap
              << "  <- expects 1000 (snapshot isolation)\n";
    db.commit(t1);
    std::cout << "  Latest committed value: " << db.latest("balance") << "\n";
}

// ─────────────────────────────────────────────
//  Scenario 2: Concurrent lock-free reads (MVCC)
// ─────────────────────────────────────────────
void scenario2() {
    std::cout << "\n-- Scenario 2: Concurrent Lock-Free Reads (MVCC) --\n";
    Database db;
    db.bootstrap("price", "499");

    std::mutex printMtx;
    std::vector<std::thread> readers;

    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&db, &printMtx, i]() {
            uint64_t xid = db.begin();
            std::string val = db.read(xid, "price");
            { std::lock_guard<std::mutex> lk(printMtx);
              std::cout << "  Reader-" << i << " (xid=" << xid << ") price=" << val << "\n"; }
            db.commit(xid);
        });
    }
    for (auto& t : readers) t.join();
    std::cout << "  All readers finished -- no locks taken, no blocking\n";
}

// ─────────────────────────────────────────────
//  Scenario 3: Strict 2PL — writer blocks writer
// ─────────────────────────────────────────────
void scenario3() {
    std::cout << "\n-- Scenario 3: Strict 2PL -- Writer Blocks Writer --\n";
    Database db;
    db.bootstrap("inventory", "500");

    uint64_t w1 = db.begin();
    db.write(w1, "inventory", "480");
    std::cout << "  W1 (xid=" << w1 << ") holds exclusive lock on inventory\n";

    std::thread w2thread([&db]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        uint64_t w2 = db.begin();
        std::cout << "  W2 (xid=" << w2 << ") requesting exclusive lock (will wait)...\n";
        db.write(w2, "inventory", "460");
        std::cout << "  W2 acquired lock, wrote inventory=460\n";
        db.commit(w2);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    db.commit(w1);
    std::cout << "  W1 committed, W2 can now proceed\n";
    w2thread.join();
    std::cout << "  Final inventory: " << db.latest("inventory") << "\n";
}

// ─────────────────────────────────────────────
//  Scenario 4: Deadlock Detection
//  T1 holds res_A, T2 holds res_B.
//  T1 wants res_B, T2 wants res_A -> cycle.
// ─────────────────────────────────────────────
void scenario4() {
    std::cout << "\n-- Scenario 4: Deadlock Detection (Waits-For Graph) --\n";
    Database db;
    db.bootstrap("res_A", "valA");
    db.bootstrap("res_B", "valB");

    uint64_t t1 = db.begin();
    uint64_t t2 = db.begin();

    db.write(t1, "res_A", "A_by_T1");
    std::cout << "  T1 (xid=" << t1 << ") locked res_A\n";
    db.write(t2, "res_B", "B_by_T2");
    std::cout << "  T2 (xid=" << t2 << ") locked res_B\n";

    std::thread th1([&db, t1]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::cout << "  T1 requesting res_B...\n";
        bool ok = db.write(t1, "res_B", "B_by_T1");
        if (ok) { std::cout << "  T1 got res_B\n"; db.commit(t1); }
        else      db.abort(t1);
    });

    std::thread th2([&db, t2]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::cout << "  T2 requesting res_A...\n";
        bool ok = db.write(t2, "res_A", "A_by_T2");
        if (ok) { std::cout << "  T2 got res_A\n"; db.commit(t2); }
        else      db.abort(t2);
    });

    th1.join();
    th2.join();
}

int main() {
    std::cout << std::unitbuf;
    std::cout << "=== Lab 8: MVCC + Strict 2PL + Deadlock Detection ===\n";
    scenario1();
    scenario2();
    scenario3();
    scenario4();
    return 0;
}
