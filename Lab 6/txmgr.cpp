// Lab 6: Transaction Manager — MVCC + Two-Phase Locking
// Windows-portable version: uses Win32 threads (works on any modern Windows).
//
// Compile: g++ -std=c++17 -o txmgr txmgr.cpp
// Run:     ./txmgr
//
// Linux/macOS/Modern MinGW port: see txmgr_posix.cpp (uses std::thread, std::mutex)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <string>
#include <stdexcept>
#include <chrono>
#include <functional>

// ════════════════════════════════════════════════════════════════════
// 1. Transaction state
// ════════════════════════════════════════════════════════════════════

using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;
};

static CRITICAL_SECTION                  g_tx_cs;
static uint64_t                          g_next_xid = 1;
static std::unordered_map<TxID, Transaction> g_transactions;

TxID begin_transaction() {
    EnterCriticalSection(&g_tx_cs);
    TxID xid = g_next_xid++;
    g_transactions[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
    LeaveCriticalSection(&g_tx_cs);
    return xid;
}

bool is_committed(TxID xid) {
    EnterCriticalSection(&g_tx_cs);
    bool ok = g_transactions.count(xid) && g_transactions.at(xid).status == TxStatus::COMMITTED;
    LeaveCriticalSection(&g_tx_cs);
    return ok;
}

// ════════════════════════════════════════════════════════════════════
// 2. MVCC version chain
// ════════════════════════════════════════════════════════════════════

struct RowVersion {
    std::string value;
    TxID        xmin = 0;
    TxID        xmax = 0;
};

static CRITICAL_SECTION                                   g_heap_cs;
static std::unordered_map<RowKey, std::list<RowVersion>>  g_heap;

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_ok = (v.xmin == reader_xid)
                || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;
    if (v.xmax == 0) return true;

    EnterCriticalSection(&g_tx_cs);
    auto it = g_transactions.find(v.xmax);
    bool committed = it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
    bool own = (v.xmax == reader_xid);
    LeaveCriticalSection(&g_tx_cs);

    if (own) return true;
    if (committed && v.xmax < snapshot_xid) return false;
    return true;
}

struct ReadResult {
    std::string value;
    bool        found = false;
    ReadResult() = default;
    ReadResult(std::string v, bool f) : value(std::move(v)), found(f) {}
};

ReadResult mvcc_read_key(const RowKey& key, TxID xid) {
    EnterCriticalSection(&g_heap_cs);
    TxID snap;
    { EnterCriticalSection(&g_tx_cs); snap = g_transactions.at(xid).snapshot_xid; LeaveCriticalSection(&g_tx_cs); }

    auto it = g_heap.find(key);
    if (it == g_heap.end()) { LeaveCriticalSection(&g_heap_cs); return ReadResult{}; }
    for (auto& v : it->second)
        if (is_visible(v, snap, xid)) {
            ReadResult r{v.value, true};
            LeaveCriticalSection(&g_heap_cs);
            return r;
        }
    LeaveCriticalSection(&g_heap_cs);
    return ReadResult{};
}

void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    EnterCriticalSection(&g_heap_cs);
    g_heap[key].push_front({value, xid, 0});
    LeaveCriticalSection(&g_heap_cs);
}

void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    EnterCriticalSection(&g_heap_cs);
    TxID snap;
    { EnterCriticalSection(&g_tx_cs); snap = g_transactions.at(xid).snapshot_xid; LeaveCriticalSection(&g_tx_cs); }

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
    LeaveCriticalSection(&g_heap_cs);
}

// ════════════════════════════════════════════════════════════════════
// 3. Lock Manager (Strict 2PL) — uses Win32 CRITICAL_SECTION per row + events for wait
// ════════════════════════════════════════════════════════════════════

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID     xid;
    LockMode mode;
    bool     granted = false;
};

struct LockQueue {
    std::list<LockRequest>  requests;
    CRITICAL_SECTION        mu;
    HANDLE                  change_event;     // signaled when a lock is released
    bool                    initialized = false;
};

static std::unordered_map<RowKey, LockQueue>  g_lock_table;
static CRITICAL_SECTION                       g_lm_cs;
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

bool has_cycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        if (!visited.insert(node).second) return false;
        auto it = graph.find(node);
        if (it == graph.end()) return false;
        for (TxID nb : it->second) {
            if (nb == start) return true;
            if (dfs(nb)) return true;
        }
        return false;
    };
    return dfs(start);
}

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

LockQueue& get_queue(const RowKey& key) {
    EnterCriticalSection(&g_lm_cs);
    auto& lq = g_lock_table[key];
    if (!lq.initialized) {
        InitializeCriticalSection(&lq.mu);
        lq.change_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);   // auto-reset
        lq.initialized = true;
    }
    LeaveCriticalSection(&g_lm_cs);
    return lq;
}

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    // 2PL: cannot acquire in shrinking phase
    EnterCriticalSection(&g_tx_cs);
    bool shrinking = g_transactions.at(xid).in_shrinking;
    LeaveCriticalSection(&g_tx_cs);
    if (shrinking) throw std::runtime_error("2PL violation: acquire in shrinking phase");

    LockQueue& lq = get_queue(key);
    EnterCriticalSection(&lq.mu);

    // Already hold EXCLUSIVE? Done.
    for (auto& r : lq.requests) {
        if (r.xid == xid && r.granted && r.mode == LockMode::EXCLUSIVE) {
            LeaveCriticalSection(&lq.mu);
            return;
        }
    }

    lq.requests.push_back({xid, mode, false});
    auto it_self = std::prev(lq.requests.end());

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        for (auto it = lq.requests.begin(); it != it_self; ++it) {
            if (!it->granted) continue;
            if (mode == LockMode::EXCLUSIVE || it->mode == LockMode::EXCLUSIVE) {
                if (it->xid != xid) { conflict = true; blocking.insert(it->xid); }
            }
        }

        if (!conflict) {
            it_self->granted = true;
            EnterCriticalSection(&g_lm_cs);
            g_waits_for.erase(xid);
            LeaveCriticalSection(&g_lm_cs);
            LeaveCriticalSection(&lq.mu);
            return;
        }

        // Deadlock check
        {
            EnterCriticalSection(&g_lm_cs);
            g_waits_for[xid] = blocking;
            if (has_cycle(xid, g_waits_for)) {
                g_waits_for.erase(xid);
                lq.requests.erase(it_self);
                LeaveCriticalSection(&g_lm_cs);
                LeaveCriticalSection(&lq.mu);
                throw DeadlockException(xid);
            }
            LeaveCriticalSection(&g_lm_cs);
        }

        // Wait for lock state change (auto-reset event — wakes one waiter)
        LeaveCriticalSection(&lq.mu);
        WaitForSingleObject(lq.change_event, 50);
        EnterCriticalSection(&lq.mu);
        // re-validate: my request may have been removed externally
        bool still_here = false;
        for (auto it = lq.requests.begin(); it != lq.requests.end(); ++it)
            if (it == it_self) { still_here = true; break; }
        if (!still_here) {
            // Removed (shouldn't happen in our paths, but be safe)
            LeaveCriticalSection(&lq.mu);
            return;
        }
    }
}

void release_locks(TxID xid) {
    EnterCriticalSection(&g_tx_cs);
    if (g_transactions.count(xid))
        g_transactions.at(xid).in_shrinking = true;
    LeaveCriticalSection(&g_tx_cs);

    for (auto& kv : g_lock_table) {
        auto& lq = kv.second;
        EnterCriticalSection(&lq.mu);
        auto before = lq.requests.size();
        lq.requests.remove_if([&](const LockRequest& r){ return r.xid == xid; });
        auto after = lq.requests.size();
        LeaveCriticalSection(&lq.mu);
        if (before != after) SetEvent(lq.change_event);
    }
    EnterCriticalSection(&g_lm_cs);
    g_waits_for.erase(xid);
    LeaveCriticalSection(&g_lm_cs);
}

// ════════════════════════════════════════════════════════════════════
// 4. Transaction Manager
// ════════════════════════════════════════════════════════════════════

class TransactionManager {
public:
    void init() {
        InitializeCriticalSection(&g_tx_cs);
        InitializeCriticalSection(&g_heap_cs);
        InitializeCriticalSection(&g_lm_cs);
    }

    TxID begin() { return begin_transaction(); }

    ReadResult read(TxID xid, const RowKey& key) {
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

    void commit(TxID xid) {
        EnterCriticalSection(&g_tx_cs);
        g_transactions.at(xid).status = TxStatus::COMMITTED;
        LeaveCriticalSection(&g_tx_cs);
        release_locks(xid);
        std::cout << "  [TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        EnterCriticalSection(&g_heap_cs);
        for (auto& kv : g_heap) {
            for (auto& v : kv.second) {
                if (v.xmin == xid) v.xmax = xid;
                if (v.xmax == xid) v.xmax = 0;
            }
        }
        LeaveCriticalSection(&g_heap_cs);
        EnterCriticalSection(&g_tx_cs);
        g_transactions.at(xid).status = TxStatus::ABORTED;
        LeaveCriticalSection(&g_tx_cs);
        release_locks(xid);
        std::cout << "  [TX " << xid << "] ABORTED\n";
    }
};

// ════════════════════════════════════════════════════════════════════
// Win32 thread helper
// ════════════════════════════════════════════════════════════════════

struct ThreadArg {
    void (*fn)(void*);
    void* arg;
};

static DWORD WINAPI thread_trampoline(LPVOID p) {
    auto* a = static_cast<ThreadArg*>(p);
    a->fn(a->arg);
    delete a;
    return 0;
}

void spawn(void (*fn)(void*), void* arg) {
    auto* a = new ThreadArg{fn, arg};
    CreateThread(nullptr, 0, thread_trampoline, a, 0, nullptr);
}

static void sleep_ms(int ms) {
    Sleep(ms);
}

// ════════════════════════════════════════════════════════════════════
// Demo scenarios
// ════════════════════════════════════════════════════════════════════

static TransactionManager* g_tm = nullptr;

static void print_val(const ReadResult& v, TxID xid, const RowKey& key) {
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (v.found ? v.value : "<not visible>") << "\n";
}

// ── Scenario 1: MVCC snapshot isolation ──
static void scenario1() {
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    TxID t1 = g_tm->begin();
    g_tm->insert(t1, "balance", "1000");
    g_tm->commit(t1);

    TxID t2 = g_tm->begin();
    TxID t3 = g_tm->begin();

    g_tm->update(t3, "balance", "2000");
    g_tm->commit(t3);

    auto v = g_tm->read(t2, "balance");
    print_val(v, t2, "balance");
    g_tm->commit(t2);
}

// ── Scenario 2: Concurrent shared locks (sequential — all SHARED are compatible) ──
static void scenario2() {
    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    TxID t4 = g_tm->begin();
    TxID t5 = g_tm->begin();
    print_val(g_tm->read(t4, "balance"), t4, "balance");
    print_val(g_tm->read(t5, "balance"), t5, "balance");
    g_tm->commit(t4);
    g_tm->commit(t5);
}

// ── Scenario 3: Exclusive blocks shared, then released ──
struct Arg3 { TxID xid; const char* key; };
static void reader_thread3(void* p) {
    auto* a = static_cast<Arg3*>(p);
    std::cout << "  [TX " << a->xid << "] waiting for shared lock on " << a->key << "...\n";
    auto v = g_tm->read(a->xid, a->key);
    print_val(v, a->xid, a->key);
    g_tm->commit(a->xid);
}
static void scenario3() {
    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    TxID t6 = g_tm->begin();
    g_tm->update(t6, "balance", "3000");

    TxID t7 = g_tm->begin();
    Arg3* a = new Arg3{t7, "balance"};
    spawn(reader_thread3, a);
    sleep_ms(50);
    g_tm->commit(t6);
    sleep_ms(200);   // give reader time to finish
}

// ── Scenario 4: Deadlock detection ──
struct Arg8 { TxID xid; const char* key; };
static void th8(void* p) {
    auto* a = static_cast<Arg8*>(p);
    try {
        std::cout << "  [TX " << a->xid << "] wants EXCLUSIVE on " << a->key << "\n";
        g_tm->update(a->xid, a->key, "updated");
        g_tm->commit(a->xid);
    } catch (DeadlockException& e) {
        std::cout << "  " << e.what() << "\n";
        g_tm->abort(a->xid);
    }
}

static void scenario4() {
    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    TxID t8 = g_tm->begin();
    TxID t9 = g_tm->begin();
    g_tm->insert(t8, "A", "val_a");
    g_tm->insert(t9, "B", "val_b");

    Arg8* a8 = new Arg8{t8, "B"};
    spawn(th8, a8);
    sleep_ms(20);

    try {
        std::cout << "  [TX " << t9 << "] wants EXCLUSIVE on A\n";
        g_tm->update(t9, "A", "updated_a");
        g_tm->commit(t9);
    } catch (DeadlockException& e) {
        std::cout << "  " << e.what() << "\n";
        g_tm->abort(t9);
    }
    sleep_ms(300);
}

int main() {
    TransactionManager tm;
    tm.init();
    g_tm = &tm;

    scenario1();
    scenario2();
    scenario3();
    scenario4();

    std::cout << "\nAll scenarios complete.\n";
    Sleep(200);
    return 0;
}