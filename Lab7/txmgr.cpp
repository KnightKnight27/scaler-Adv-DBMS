#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <stdexcept>
#include <string>
#include <sstream>
#include <cassert>
#include <functional>
#include <windows.h>

using TxID   = unsigned long long;
using RowKey = std::string;

// ─── Simple RAII wrappers for Win32 synchronization ──────────────────────────

struct Mutex {
    CRITICAL_SECTION cs;
    Mutex()  { InitializeCriticalSection(&cs); }
    ~Mutex() { DeleteCriticalSection(&cs); }
    void lock()   { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
};

struct LockGuard {
    Mutex& m;
    explicit LockGuard(Mutex& mx) : m(mx) { m.lock(); }
    ~LockGuard() { m.unlock(); }
};

struct CondVar {
    CONDITION_VARIABLE cv;
    CondVar() { InitializeConditionVariable(&cv); }
    void wait(Mutex& m)   { SleepConditionVariableCS(&cv, &m.cs, INFINITE); }
    void notify_all()     { WakeAllConditionVariable(&cv); }
};

// ─── Atomic XID (manual, using Windows Interlocked) ──────────────────────────

static volatile LONG g_next_xid = 0;
static TxID next_xid() {
    return (TxID)InterlockedIncrement(&g_next_xid);
}

// ─── Transaction state ────────────────────────────────────────────────────────

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;
    TxStatus status;
    bool     in_shrinking;
};

static Mutex                            g_tx_mutex;
static std::unordered_map<TxID, Transaction> g_transactions;

TxID begin_transaction() {
    LockGuard lk(g_tx_mutex);
    TxID xid  = next_xid();
    TxID snap = xid;
    g_transactions[xid] = Transaction{xid, snap, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    LockGuard lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    LockGuard lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::ABORTED;
}

// ─── MVCC version chain ───────────────────────────────────────────────────────

struct RowVersion {
    std::string value;
    TxID        xmin;
    TxID        xmax;
};

static Mutex                                             g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_ok = (v.xmin == reader_xid)
                 || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;

    if (v.xmax == 0) return true;
    bool xmax_invisible = (v.xmax == reader_xid)
                        || (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_invisible;
}

bool mvcc_read_key(const RowKey& key, TxID xid, std::string& out) {
    LockGuard lk(g_heap_mutex);
    TxID snap;
    {
        LockGuard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return false;
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid)) { out = v.value; return true; }
    }
    return false;
}

void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    LockGuard lk(g_heap_mutex);
    g_heap[key].push_front({value, xid, 0});
}

void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    LockGuard lk(g_heap_mutex);
    TxID snap;
    {
        LockGuard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;
                break;
            }
        }
    }
    g_heap[key].push_front({new_value, xid, 0});
}

void mvcc_delete(const RowKey& key, TxID xid) {
    LockGuard lk(g_heap_mutex);
    TxID snap;
    {
        LockGuard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid) && v.xmax == 0) { v.xmax = xid; return; }
    }
}

// ─── Lock Manager (Strict 2PL) ────────────────────────────────────────────────

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID     xid;
    LockMode mode;
    bool     granted;
};

struct LockQueue {
    std::list<LockRequest> requests;
    Mutex   mu;
    CondVar cv;
};

static Mutex                                              g_lm_mutex;
static std::unordered_map<RowKey, LockQueue*>             g_lock_table;
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

bool has_cycle(TxID start,
               const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited, stk;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stk.insert(node);
        auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxID nb : it->second) {
                if (!visited.count(nb) && dfs(nb)) return true;
                if (stk.count(nb)) return true;
            }
        }
        stk.erase(node);
        return false;
    };
    return dfs(start);
}

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

LockQueue& get_or_create_queue(const RowKey& key) {
    LockGuard lk(g_lm_mutex);
    auto it = g_lock_table.find(key);
    if (it == g_lock_table.end()) {
        g_lock_table[key] = new LockQueue();
        return *g_lock_table[key];
    }
    return *it->second;
}

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    {
        LockGuard lk(g_tx_mutex);
        if (g_transactions.at(xid).in_shrinking)
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
    }

    LockQueue& lq = get_or_create_queue(key);
    LockGuard ul(lq.mu);

    for (auto& r : lq.requests) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED)      return;
            if (r.mode == LockMode::EXCLUSIVE) return;
        }
    }

    lq.requests.push_back({xid, mode, false});
    LockRequest& my_req = lq.requests.back();

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;

        for (auto& r : lq.requests) {
            if (&r == &my_req) break;
            if (!r.granted) continue;
            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                if (r.xid != xid) { conflict = true; blocking.insert(r.xid); }
            }
        }

        if (!conflict) {
            my_req.granted = true;
            {
                LockGuard lk2(g_lm_mutex);
                g_waits_for.erase(xid);
            }
            return;
        }

        {
            LockGuard lk2(g_lm_mutex);
            g_waits_for[xid] = blocking;
            if (has_cycle(xid, g_waits_for)) {
                g_waits_for.erase(xid);
                lq.requests.remove_if([&](const LockRequest& r) {
                    return r.xid == xid && !r.granted;
                });
                throw DeadlockException(xid);
            }
        }

        lq.cv.wait(lq.mu);
    }
}

void release_locks(TxID xid) {
    {
        LockGuard lk(g_tx_mutex);
        if (g_transactions.count(xid))
            g_transactions.at(xid).in_shrinking = true;
    }

    {
        LockGuard lk(g_lm_mutex);
        for (auto& kv : g_lock_table) {
            LockQueue& lq = *kv.second;
            LockGuard ul(lq.mu);
            lq.requests.remove_if([&](const LockRequest& r) { return r.xid == xid; });
            lq.cv.notify_all();
        }
        g_waits_for.erase(xid);
    }
}

// ─── Transaction Manager (public API) ────────────────────────────────────────

class TransactionManager {
public:
    TxID begin() { return begin_transaction(); }

    bool read(TxID xid, const RowKey& key, std::string& out) {
        acquire_lock(key, xid, LockMode::SHARED);
        return mvcc_read_key(key, xid, out);
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
            LockGuard lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::COMMITTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        {
            LockGuard lk(g_heap_mutex);
            for (auto& kv : g_heap) {
                for (auto& v : kv.second) {
                    if (v.xmin == xid) v.xmax = xid;
                    if (v.xmax == xid && v.xmin != xid) v.xmax = 0;
                }
            }
        }
        {
            LockGuard lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::ABORTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] ABORTED\n";
    }
};

void print_val(bool found, const std::string& val, TxID xid, const RowKey& key) {
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (found ? val : "<not visible>") << "\n";
}

// ─── Thread helper (Win32) ───────────────────────────────────────────────────

struct ThreadCtx {
    std::function<void()> fn;
};

static DWORD WINAPI thread_entry(LPVOID arg) {
    ThreadCtx* ctx = static_cast<ThreadCtx*>(arg);
    ctx->fn();
    delete ctx;
    return 0;
}

HANDLE spawn(std::function<void()> fn) {
    ThreadCtx* ctx = new ThreadCtx{fn};
    HANDLE h = CreateThread(NULL, 0, thread_entry, ctx, 0, NULL);
    return h;
}

// ─── Demo Scenarios ───────────────────────────────────────────────────────────

int main() {
    TransactionManager tm;

    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);

        TxID t2 = tm.begin();
        TxID t3 = tm.begin();

        tm.update(t3, "balance", "2000");
        tm.commit(t3);

        std::string val;
        bool found = tm.read(t2, "balance", val);
        print_val(found, val, t2, "balance");
        tm.commit(t2);
    }

    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();
        std::string v4, v5;
        print_val(tm.read(t4, "balance", v4), v4, t4, "balance");
        print_val(tm.read(t5, "balance", v5), v5, t5, "balance");
        tm.commit(t4);
        tm.commit(t5);
    }

    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxID t6 = tm.begin();
        tm.update(t6, "balance", "3000");

        HANDLE reader = spawn([&tm]() {
            TxID t7 = tm.begin();
            std::cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            std::string val;
            bool found = tm.read(t7, "balance", val);
            print_val(found, val, t7, "balance");
            tm.commit(t7);
        });

        Sleep(50);
        tm.commit(t6);
        WaitForSingleObject(reader, INFINITE);
        CloseHandle(reader);
    }

    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxID ta = tm.begin();
        TxID tb = tm.begin();
        tm.insert(ta, "A", "val_a");
        tm.insert(tb, "B", "val_b");
        tm.commit(ta);
        tm.commit(tb);

        TxID t8 = tm.begin();
        TxID t9 = tm.begin();

        acquire_lock("A", t8, LockMode::EXCLUSIVE);
        acquire_lock("B", t9, LockMode::EXCLUSIVE);

        HANDLE th1 = spawn([&tm, t8]() {
            try {
                acquire_lock("B", t8, LockMode::EXCLUSIVE);
                tm.commit(t8);
            } catch (DeadlockException& e) {
                std::cout << "  " << e.what() << "\n";
                tm.abort(t8);
            }
        });

        Sleep(20);

        try {
            acquire_lock("A", t9, LockMode::EXCLUSIVE);
            tm.commit(t9);
        } catch (DeadlockException& e) {
            std::cout << "  " << e.what() << "\n";
            tm.abort(t9);
        }

        WaitForSingleObject(th1, INFINITE);
        CloseHandle(th1);
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}