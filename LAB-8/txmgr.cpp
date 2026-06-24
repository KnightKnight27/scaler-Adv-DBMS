#define _WIN32_WINNT 0x0600
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <thread>
#include <stdexcept>
#include <atomic>
#include <sstream>
#include <cassert>
#include <functional>
#include <chrono>

// ─────────────────────────────────────────────
// 0.  Win32 Threading Compatibility Shim
// ─────────────────────────────────────────────

namespace thread_compat {

class Mutex {
    CRITICAL_SECTION cs;
public:
    Mutex() { InitializeCriticalSection(&cs); }
    ~Mutex() { DeleteCriticalSection(&cs); }
    void lock() { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
    CRITICAL_SECTION* get_cs() { return &cs; }
private:
    // Prevent copying
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
};

template <typename M>
class LockGuard {
    M& m;
public:
    LockGuard(M& mutex) : m(mutex) { m.lock(); }
    ~LockGuard() { m.unlock(); }
private:
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

template <typename M>
class UniqueLock {
    M& m;
    bool locked;
public:
    UniqueLock(M& mutex) : m(mutex), locked(true) { m.lock(); }
    ~UniqueLock() { if (locked) m.unlock(); }
    void lock() { m.lock(); locked = true; }
    void unlock() { m.unlock(); locked = false; }
    M* mutex() const { return &m; }
private:
    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;
};

class ConditionVariable {
    CONDITION_VARIABLE cv;
public:
    ConditionVariable() { InitializeConditionVariable(&cv); }
    void wait(UniqueLock<Mutex>& lk) {
        SleepConditionVariableCS(&cv, lk.mutex()->get_cs(), INFINITE);
    }
    void notify_one() { WakeConditionVariable(&cv); }
    void notify_all() { WakeAllConditionVariable(&cv); }
private:
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
};

class Thread {
    HANDLE hThread;
    
    struct ThreadData {
        std::function<void()> func;
    };
    
    static DWORD WINAPI ThreadProc(LPVOID lpParam) {
        ThreadData* data = static_cast<ThreadData*>(lpParam);
        data->func();
        delete data;
        return 0;
    }
public:
    Thread() : hThread(NULL) {}
    
    template <typename F>
    explicit Thread(F&& f) {
        ThreadData* data = new ThreadData{std::move(f)};
        hThread = CreateThread(NULL, 0, ThreadProc, data, 0, NULL);
        if (hThread == NULL) {
            delete data;
            throw std::runtime_error("Failed to create thread");
        }
    }
    
    ~Thread() {
        if (hThread != NULL) {
            CloseHandle(hThread);
        }
    }
    
    void join() {
        if (hThread != NULL) {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
            hThread = NULL;
        }
    }
private:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
};

} // namespace thread_compat

using thread_compat::Mutex;
using thread_compat::LockGuard;
using thread_compat::UniqueLock;
using thread_compat::ConditionVariable;
using thread_compat::Thread;

namespace this_thread {
    inline void sleep_for(std::chrono::milliseconds ms) {
        Sleep(static_cast<DWORD>(ms.count()));
    }
}

// ─────────────────────────────────────────────
// 0.5.  Custom Optional Implementation for Compatibility
// ─────────────────────────────────────────────

struct NullOptType {};
constexpr NullOptType nullopt{};

template <typename T>
class Optional {
    bool has_val;
    T val;
public:
    Optional() : has_val(false) {}
    Optional(NullOptType) : has_val(false) {}
    Optional(const T& v) : has_val(true), val(v) {}
    Optional(T&& v) : has_val(true), val(std::move(v)) {}
    
    bool has_value() const { return has_val; }
    operator bool() const { return has_val; }
    const T& operator*() const { return val; }
    T& operator*() { return val; }
    const T* operator->() const { return &val; }
    T* operator->() { return &val; }
};

// ─────────────────────────────────────────────
// 1.  Transaction state
// ─────────────────────────────────────────────

using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;   // read snapshot: see commits < snapshot_xid
    TxStatus status;
    bool     in_shrinking;   // 2PL phase flag

    Transaction() : id(0), snapshot_xid(0), status(TxStatus::ACTIVE), in_shrinking(false) {}
    Transaction(TxID i, TxID s, TxStatus st = TxStatus::ACTIVE, bool shrink = false)
        : id(i), snapshot_xid(s), status(st), in_shrinking(shrink) {}
};

// Global transaction table
static std::atomic<TxID>                          g_next_xid{1};
static Mutex                                      g_tx_mutex;
static std::unordered_map<TxID, Transaction>      g_transactions;

TxID begin_transaction() {
    LockGuard<Mutex> lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    // snapshot: see all commits that finished before this xid
    TxID snap = xid;
    g_transactions[xid] = Transaction(xid, snap, TxStatus::ACTIVE, false);
    return xid;
}

bool is_committed(TxID xid) {
    LockGuard<Mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    LockGuard<Mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::ABORTED;
}

// ─────────────────────────────────────────────
// 2.  MVCC version chain
// ─────────────────────────────────────────────

struct RowVersion {
    std::string value;
    TxID        xmin;   // created by
    TxID        xmax;   // deleted/updated by (0 = still live)

    RowVersion(const std::string& val, TxID min, TxID max = 0)
        : value(val), xmin(min), xmax(max) {}
};

// Each logical row is a chain of versions (newest first)
static Mutex                                            g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

// Visibility check for a snapshot taken at snapshot_xid
bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    // xmin must be committed and <= snapshot
    bool xmin_ok = (v.xmin == reader_xid)          // own write
                 || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;

    // xmax: version must not have been deleted before our snapshot
    if (v.xmax == 0) return true;
    bool xmax_invisible = (v.xmax == reader_xid)   // we deleted it ourselves
                        || (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_invisible;
}

Optional<std::string> mvcc_read(TxID xid) {
    LockGuard<Mutex> lk(g_heap_mutex);
    TxID snap;
    {
        LockGuard<Mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    for (auto& pair : g_heap) {
        auto& chain = pair.second;
        for (auto& v : chain)
            if (is_visible(v, snap, xid)) return v.value;
    }
    return nullopt;
}

Optional<std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    LockGuard<Mutex> lk(g_heap_mutex);
    TxID snap;
    {
        LockGuard<Mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return nullopt;
    for (auto& v : it->second)
        if (is_visible(v, snap, xid)) return v.value;
    return nullopt;
}

// INSERT: new version, xmin=xid, xmax=0
void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    LockGuard<Mutex> lk(g_heap_mutex);
    g_heap[key].push_front(RowVersion(value, xid, 0));
}

// UPDATE: mark old visible version as xmax=xid, insert new version
void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    LockGuard<Mutex> lk(g_heap_mutex);
    TxID snap;
    {
        LockGuard<Mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;   // logically delete old version
                break;
            }
        }
    }
    g_heap[key].push_front(RowVersion(new_value, xid, 0));
}

// DELETE: mark visible version xmax=xid
void mvcc_delete(const RowKey& key, TxID xid) {
    LockGuard<Mutex> lk(g_heap_mutex);
    TxID snap;
    {
        LockGuard<Mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid) && v.xmax == 0) {
            v.xmax = xid;
            return;
        }
    }
}

// ─────────────────────────────────────────────
// 3.  Lock Manager (Strict 2PL)
// ─────────────────────────────────────────────

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID     xid;
    LockMode mode;
    bool     granted;

    LockRequest(TxID x, LockMode m, bool g = false) : xid(x), mode(m), granted(g) {}
};

struct LockQueue {
    std::list<LockRequest>  requests;
    Mutex                   mu;
    ConditionVariable       cv;
};

static Mutex                                           g_lm_mutex;
static std::unordered_map<RowKey, LockQueue>           g_lock_table;

// Waits-for graph for deadlock detection: waiter -> set of holders it's waiting on
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

bool has_cycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited, stack;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stack.insert(node);
        auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxID nb : it->second) {
                if (!visited.count(nb) && dfs(nb)) return true;
                if (stack.count(nb)) return true;
            }
        }
        stack.erase(node);
        return false;
    };
    return dfs(start);
}

// Returns true if lock granted; throws DeadlockException if cycle detected
class DeadlockException : public std::runtime_error {
public: explicit DeadlockException(TxID xid)
    : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    // 2PL: cannot acquire lock in shrinking phase
    {
        LockGuard<Mutex> lk(g_tx_mutex);
        if (g_transactions.at(xid).in_shrinking)
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
    }

    LockQueue* lq_ptr = nullptr;
    {
        LockGuard<Mutex> lk(g_lm_mutex);
        lq_ptr = &g_lock_table[key];   // Thread-safe map lookup/insertion
    }
    LockQueue& lq = *lq_ptr;
    UniqueLock<Mutex> ul(lq.mu);

    // Check if we already hold this lock
    for (auto& r : lq.requests) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED) return;  // already have shared (or better)
            if (r.mode == LockMode::EXCLUSIVE) return;  // already exclusive
            // Upgrade: shared -> exclusive — need no other shared holders
        }
    }

    // Add our request
    lq.requests.push_back(LockRequest(xid, mode, false));
    auto& my_req = lq.requests.back();

    while (true) {
        // Can we grant?
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        for (auto& r : lq.requests) {
            if (&r == &my_req) break;      // only look at earlier requests
            if (!r.granted) continue;
            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                if (r.xid != xid) { conflict = true; blocking.insert(r.xid); }
            }
        }

        if (!conflict) {
            my_req.granted = true;
            {
                LockGuard<Mutex> lk(g_lm_mutex);
                g_waits_for.erase(xid);
            }
            return;
        }

        // Record waits-for edges and check for cycle
        {
            LockGuard<Mutex> lk(g_lm_mutex);
            g_waits_for[xid] = blocking;
            if (has_cycle(xid, g_waits_for)) {
                g_waits_for.erase(xid);
                lq.requests.remove_if([&](const LockRequest& r){ return r.xid == xid && !r.granted; });
                throw DeadlockException(xid);
            }
        }

        lq.cv.wait(ul);   // wait for a signal from a release
    }
}

void release_locks(TxID xid) {
    // Shrinking phase begins — mark transaction
    {
        LockGuard<Mutex> lk(g_tx_mutex);
        if (g_transactions.count(xid))
            g_transactions.at(xid).in_shrinking = true;
    }

    // Thread-safe lock queue retrieval to avoid lock-ordering deadlock
    std::vector<LockQueue*> queues;
    {
        LockGuard<Mutex> lk(g_lm_mutex);
        for (auto& pair : g_lock_table) {
            queues.push_back(&pair.second);
        }
    }

    // Release all locks held by xid across all queues
    for (LockQueue* lq : queues) {
        UniqueLock<Mutex> ul(lq->mu);
        lq->requests.remove_if([&](const LockRequest& r){ return r.xid == xid; });
        lq->cv.notify_all();   // wake waiters
    }

    {
        LockGuard<Mutex> lk(g_lm_mutex);
        g_waits_for.erase(xid);
    }
}

// ─────────────────────────────────────────────
// 4.  Transaction Manager (public API)
// ─────────────────────────────────────────────

class TransactionManager {
public:
    TxID begin() { return begin_transaction(); }

    Optional<std::string> read(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::SHARED);
        return mvcc_read_key(key, xid);
    }

    void insert(TxID xid, const RowKey& key, const std::string& value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_insert(key, value, xid);
    }

    void update(TxID xid, const RowKey& key, const std::string& value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_update(key, value, xid);
    }

    void remove(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_delete(key, xid);
    }

    void commit(TxID xid) {
        {
            LockGuard<Mutex> lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::COMMITTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        // Roll back: mark all versions written by xid as invisible
        {
            LockGuard<Mutex> lk(g_heap_mutex);
            for (auto& pair : g_heap) {
                auto& chain = pair.second;
                for (auto& v : chain) {
                    if (v.xmin == xid) {
                        v.xmax = xid;  // make own inserts invisible
                    } else if (v.xmax == xid) {
                        v.xmax = 0;    // undo own deletes
                    }
                }
            }
        }
        {
            LockGuard<Mutex> lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::ABORTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] ABORTED\n";
    }
};

// ─────────────────────────────────────────────
// 5.  Demo scenarios
// ─────────────────────────────────────────────

void print_val(const Optional<std::string>& v, TxID xid, const RowKey& key) {
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (v ? *v : "<not visible>") << "\n";
}

int main() {
    TransactionManager tm;

    // ── Scenario 1: Basic MVCC snapshot isolation ──
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);

        TxID t2 = tm.begin();   // snapshot after t1 committed
        TxID t3 = tm.begin();

        // t3 updates balance — t2 should still see old value
        tm.update(t3, "balance", "2000");
        tm.commit(t3);

        auto v = tm.read(t2, "balance");
        print_val(v, t2, "balance");   // expects 1000 (t3 committed after t2 started)
        tm.commit(t2);
    }

    // ── Scenario 2: Two-Phase Locking — concurrent reads ──
    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();
        print_val(tm.read(t4, "balance"), t4, "balance");  // shared lock
        print_val(tm.read(t5, "balance"), t5, "balance");  // shared lock — both granted
        tm.commit(t4);
        tm.commit(t5);
    }

    // ── Scenario 3: Exclusive lock blocks shared ──
    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxID t6 = tm.begin();
        tm.update(t6, "balance", "3000");  // holds EXCLUSIVE lock on "balance"

        // t7 runs on a separate thread, will block until t6 commits
        Thread reader([&]() {
            TxID t7 = tm.begin();
            std::cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            auto v = tm.read(t7, "balance");
            print_val(v, t7, "balance");  // sees 3000 after t6 commits
            tm.commit(t7);
        });

        this_thread::sleep_for(std::chrono::milliseconds(50));
        tm.commit(t6);    // releases lock, unblocks t7
        reader.join();
    }

    // ── Scenario 4: Deadlock detection ──
    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxID ta = tm.begin();
        TxID tb = tm.begin();

        // ta holds lock on "A", tb holds lock on "B"
        tm.insert(ta, "A", "val_a");
        tm.insert(tb, "B", "val_b");
        tm.commit(ta);
        tm.commit(tb);

        TxID t8 = tm.begin();
        TxID t9 = tm.begin();

        acquire_lock("A", t8, LockMode::EXCLUSIVE);
        acquire_lock("B", t9, LockMode::EXCLUSIVE);

        // t8 wants B (held by t9), t9 wants A (held by t8) → deadlock
        Thread th1([&]() {
            try {
                acquire_lock("B", t8, LockMode::EXCLUSIVE);
                tm.commit(t8);
            } catch (DeadlockException& e) {
                std::cout << "  " << e.what() << "\n";
                tm.abort(t8);
            }
        });

        this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            acquire_lock("A", t9, LockMode::EXCLUSIVE);
            tm.commit(t9);
        } catch (DeadlockException& e) {
            std::cout << "  " << e.what() << "\n";
            tm.abort(t9);
        }

        th1.join();
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}
