#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <queue>

// ── Type aliases ──────────────────────────────────────────────────────────────
using TxID   = uint64_t;
using KeyT   = std::string;
using ValT   = std::string;

// ── MVCC version node ─────────────────────────────────────────────────────────
struct VersionNode {
    TxID  writer;
    ValT  data;
    std::shared_ptr<VersionNode> older;

    VersionNode(TxID w, ValT d)
        : writer(w), data(std::move(d)), older(nullptr) {}
};

// ── Per-record state (locks + version chain head) ─────────────────────────────
struct RecordState {
    std::shared_ptr<VersionNode> head;       // newest version first
    TxID                         xlock = 0;  // exclusive-lock holder (0 = none)
    std::unordered_set<TxID>     slocks;     // shared-lock holders
};

// ── Transaction lifecycle ──────────────────────────────────────────────────────
enum class TxPhase { ACTIVE, COMMITTED, ABORTED };

// ── TransactionManager ────────────────────────────────────────────────────────
class TransactionManager {
public:
    TransactionManager() {
        deadlockThread_ = std::thread(&TransactionManager::detectDeadlocks, this);
    }

    ~TransactionManager() {
        alive_.store(false);
        if (deadlockThread_.joinable()) deadlockThread_.join();
    }

    // Seed a key with an initial value (call before any transaction starts)
    void seed(const KeyT& key, const ValT& initial) {
        auto node = std::make_shared<VersionNode>(0 /*system tx*/, initial);
        auto& rec = records_[key];
        rec.head = node;
    }

    // Begin a new transaction; return its ID
    TxID begin() {
        std::lock_guard<std::mutex> lk(mtx_);
        TxID id = nextID_++;
        phase_[id] = TxPhase::ACTIVE;
        std::cout << "[TxMgr] BEGIN  tx=" << id << "\n";
        return id;
    }

    // Strict-2PL write: acquire X-lock, prepend version node
    bool write(TxID tx, const KeyT& key, const ValT& val) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (phase_[tx] != TxPhase::ACTIVE) return false;

        auto& rec = records_[key];

        // Already hold X-lock → just append new version
        if (rec.xlock == tx) {
            auto node = std::make_shared<VersionNode>(tx, val);
            node->older = rec.head;
            rec.head = node;
            std::cout << "[Tx" << tx << "] WRITE " << key << "=" << val
                      << " (already holds X-lock)\n";
            return true;
        }

        // Lock is free
        if (rec.xlock == 0 && rec.slocks.empty()) {
            rec.xlock = tx;
            heldLocks_[tx].insert(key);
            auto node = std::make_shared<VersionNode>(tx, val);
            node->older = rec.head;
            rec.head = node;
            std::cout << "[Tx" << tx << "] WRITE " << key << "=" << val
                      << " (acquired X-lock)\n";
            return true;
        }

        // Must wait → enqueue and spin
        waitQueue_[key].push({tx, true /*exclusive*/});
        std::cout << "[Tx" << tx << "] WAITING for X-lock on " << key << "\n";

        while (alive_ && phase_[tx] == TxPhase::ACTIVE) {
            bool myTurn = (!waitQueue_[key].empty() &&
                           waitQueue_[key].front().first == tx);
            bool lockFree = (rec.xlock == 0 && rec.slocks.empty());
            if (myTurn && lockFree) {
                waitQueue_[key].pop();
                rec.xlock = tx;
                heldLocks_[tx].insert(key);
                auto node = std::make_shared<VersionNode>(tx, val);
                node->older = rec.head;
                rec.head = node;
                std::cout << "[Tx" << tx << "] WRITE " << key << "=" << val
                          << " (lock granted after wait)\n";
                return true;
            }
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            lk.lock();
        }
        return false;
    }

    // Strict-2PL read: acquire S-lock, walk version chain for visible version
    std::pair<bool, ValT> read(TxID tx, const KeyT& key) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (phase_[tx] != TxPhase::ACTIVE) return {false, ""};

        auto& rec = records_[key];

        auto readVisible = [&]() -> std::pair<bool, ValT> {
            for (auto v = rec.head; v; v = v->older) {
                if (v->writer == tx ||
                    v->writer == 0  ||
                    phase_[v->writer] == TxPhase::COMMITTED)
                    return {true, v->data};
            }
            return {false, ""};
        };

        // Already hold any lock on this key
        if (rec.xlock == tx || rec.slocks.count(tx)) {
            auto res = readVisible();
            if (res.first)
                std::cout << "[Tx" << tx << "] READ  " << key << "=" << res.second
                          << " (already locked)\n";
            return res;
        }

        // No exclusive owner → grant S-lock immediately
        if (rec.xlock == 0) {
            rec.slocks.insert(tx);
            heldLocks_[tx].insert(key);
            auto res = readVisible();
            if (res.first)
                std::cout << "[Tx" << tx << "] READ  " << key << "=" << res.second
                          << " (acquired S-lock)\n";
            return res;
        }

        // Must wait
        waitQueue_[key].push({tx, false /*shared*/});
        std::cout << "[Tx" << tx << "] WAITING for S-lock on " << key << "\n";

        while (alive_ && phase_[tx] == TxPhase::ACTIVE) {
            bool myTurn  = (!waitQueue_[key].empty() &&
                            waitQueue_[key].front().first == tx);
            bool lockFree = (rec.xlock == 0);
            if (myTurn && lockFree) {
                waitQueue_[key].pop();
                rec.slocks.insert(tx);
                heldLocks_[tx].insert(key);
                auto res = readVisible();
                if (res.first)
                    std::cout << "[Tx" << tx << "] READ  " << key << "=" << res.second
                              << " (S-lock granted after wait)\n";
                return res;
            }
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            lk.lock();
        }
        return {false, ""};
    }

    // Commit: mark committed, release all locks
    void commit(TxID tx) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (phase_[tx] != TxPhase::ACTIVE) return;
        phase_[tx] = TxPhase::COMMITTED;
        releaseLocks(tx);
        std::cout << "[TxMgr] COMMIT tx=" << tx << " ✓\n";
    }

    // Abort: mark aborted, release all locks
    void abort(TxID tx) {
        std::lock_guard<std::mutex> lk(mtx_);
        abortInternal(tx);
    }

private:
    // ── Internal helpers ─────────────────────────────────────────────────────

    void releaseLocks(TxID tx) {
        for (const auto& key : heldLocks_[tx]) {
            auto& rec = records_[key];
            if (rec.xlock == tx) rec.xlock = 0;
            rec.slocks.erase(tx);
            // clean up any stale waiter entry for this tx
            auto& wq = waitQueue_[key];
            if (!wq.empty() && wq.front().first == tx) wq.pop();
        }
        heldLocks_.erase(tx);
    }

    void abortInternal(TxID tx) {
        phase_[tx] = TxPhase::ABORTED;
        releaseLocks(tx);
        std::cout << "[TxMgr] ABORT  tx=" << tx << " (deadlock victim)\n";
    }

    // ── Wait-for graph cycle detection (DFS) ─────────────────────────────────

    bool dfsCycle(TxID node,
                  std::unordered_map<TxID, std::unordered_set<TxID>>& wfg,
                  std::unordered_set<TxID>& seen,
                  std::unordered_set<TxID>& path,
                  TxID& victim) {
        seen.insert(node);
        path.insert(node);
        for (TxID nb : wfg[node]) {
            if (path.count(nb)) { victim = node; return true; }
            if (!seen.count(nb) && dfsCycle(nb, wfg, seen, path, victim)) return true;
        }
        path.erase(node);
        return false;
    }

    // ── Background deadlock detector ──────────────────────────────────────────

    void detectDeadlocks() {
        while (alive_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            std::lock_guard<std::mutex> lk(mtx_);

            // Build wait-for graph
            std::unordered_map<TxID, std::unordered_set<TxID>> wfg;
            for (auto& [key, wq] : waitQueue_) {
                if (wq.empty()) continue;
                auto& rec = records_[key];

                std::unordered_set<TxID> owners;
                if (rec.xlock) owners.insert(rec.xlock);
                for (TxID s : rec.slocks) owners.insert(s);

                std::queue<std::pair<TxID,bool>> tmp = wq;
                while (!tmp.empty()) {
                    TxID waiter = tmp.front().first;
                    for (TxID owner : owners)
                        if (waiter != owner) wfg[waiter].insert(owner);
                    owners.insert(waiter);
                    tmp.pop();
                }
            }

            std::unordered_set<TxID> seen, path;
            TxID victim = 0;
            for (auto& [tx, _] : wfg) {
                if (!seen.count(tx) && dfsCycle(tx, wfg, seen, path, victim)) {
                    std::cout << "[Deadlock] Cycle detected — aborting tx=" << victim << "\n";
                    abortInternal(victim);
                    break;
                }
            }
        }
    }

    // ── Data members ──────────────────────────────────────────────────────────

    std::atomic<TxID>   nextID_{1};
    std::atomic<bool>   alive_{true};
    std::mutex          mtx_;
    std::thread         deadlockThread_;

    std::unordered_map<TxID,  TxPhase>                             phase_;
    std::unordered_map<KeyT,  RecordState>                         records_;
    std::unordered_map<TxID,  std::unordered_set<KeyT>>            heldLocks_;
    std::unordered_map<KeyT,  std::queue<std::pair<TxID,bool>>>    waitQueue_;
};

// ── Demo scenarios ────────────────────────────────────────────────────────────

int main() {
    TransactionManager tm;

    // Seed the database with initial values
    tm.seed("balance_X", "500");
    tm.seed("balance_Y", "300");
    tm.seed("balance_Z", "800");

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 1: Successful read-modify-write (T1 commits cleanly)
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n========== Scenario 1: Clean commit ==========\n";
    {
        TxID t1 = tm.begin();
        auto [ok, val] = tm.read(t1, "balance_X");
        if (ok) {
            int newVal = std::stoi(val) + 100;
            tm.write(t1, "balance_X", std::to_string(newVal));
        }
        tm.commit(t1);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 2: Deadlock — T2 and T3 cross-lock balance_X and balance_Y
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n========== Scenario 2: Deadlock (T2 ↔ T3) ==========\n";
    {
        TxID t2 = tm.begin();
        TxID t3 = tm.begin();

        std::thread thr2([&]() {
            // T2: lock X then try Y
            if (tm.write(t2, "balance_X", "550")) {
                std::cout << "[Tx" << t2 << "] wrote balance_X=550\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(180));
                std::cout << "[Tx" << t2 << "] attempting balance_Y...\n";
                if (tm.write(t2, "balance_Y", "280"))
                    tm.commit(t2);
                else
                    std::cout << "[Tx" << t2 << "] write balance_Y failed (aborted)\n";
            }
        });

        std::thread thr3([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            // T3: lock Y then try X
            if (tm.write(t3, "balance_Y", "350")) {
                std::cout << "[Tx" << t3 << "] wrote balance_Y=350\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(180));
                std::cout << "[Tx" << t3 << "] attempting balance_X...\n";
                if (tm.write(t3, "balance_X", "480"))
                    tm.commit(t3);
                else
                    std::cout << "[Tx" << t3 << "] write balance_X failed (aborted)\n";
            }
        });

        thr2.join();
        thr3.join();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 3: MVCC read — T4 reads committed version while T5 writes
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n========== Scenario 3: MVCC read isolation ==========\n";
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();

        // T5 writes a new version of balance_Z but does NOT commit yet
        tm.write(t5, "balance_Z", "999");

        // T4 should read the last committed version (800), not T5's dirty write
        auto [ok, val] = tm.read(t4, "balance_Z");
        std::cout << "[Tx" << t4 << "] sees balance_Z=" << val
                  << " (expected 800 — committed snapshot)\n";

        tm.commit(t5);
        tm.commit(t4);

        // After T5 commits, a fresh read should see 999
        TxID t6 = tm.begin();
        auto [ok2, val2] = tm.read(t6, "balance_Z");
        std::cout << "[Tx" << t6 << "] sees balance_Z=" << val2
                  << " (expected 999 — T5 now committed)\n";
        tm.commit(t6);
    }

    return 0;
}
