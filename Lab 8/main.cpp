#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <atomic>
#include <functional>
#include <chrono>

using namespace std;

using TxID = uint64_t;
using RowKey = string;

enum class TxStatus
{
  ACTIVE,
  COMMITTED,
  ABORTED
};

struct Transaction
{
  TxID xid;
  TxID snapshot_xid;
  TxStatus status = TxStatus::ACTIVE;
  bool in_shrinking = false;
};

static atomic<TxID> g_next_xid{1};
static mutex g_tx_mutex;
static unordered_map<TxID, Transaction> g_transactions;

TxID begin_transaction()
{
  lock_guard<mutex> lk(g_tx_mutex);
  TxID xid = g_next_xid.fetch_add(1);
  // snapshot_xid == own xid: transactions with a higher xid are invisible
  g_transactions[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
  return xid;
}

bool is_committed(TxID xid)
{
  lock_guard<mutex> lk(g_tx_mutex);
  auto it = g_transactions.find(xid);
  return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

struct RowVersion
{
  string value;
  TxID xmin; // created by
  TxID xmax; // deleted/superseded by (0 = still live)
};

static mutex g_heap_mutex;
static unordered_map<RowKey, list<RowVersion>> g_heap;

bool is_visible(const RowVersion &v, TxID snapshot_xid, TxID reader_xid)
{
  bool xmin_ok = (v.xmin == reader_xid) || (is_committed(v.xmin) && v.xmin < snapshot_xid);
  if (!xmin_ok)
    return false;
  if (v.xmax == 0)
    return true;
  bool deletion_visible = (v.xmax == reader_xid) || (is_committed(v.xmax) && v.xmax < snapshot_xid);
  return !deletion_visible;
}

static TxID snapshot_of(TxID xid)
{
  lock_guard<mutex> lk(g_tx_mutex);
  return g_transactions.at(xid).snapshot_xid;
}

string mvcc_read(const RowKey &key, TxID xid)
{
  lock_guard<mutex> lk(g_heap_mutex);
  TxID snap = snapshot_of(xid);
  auto it = g_heap.find(key);
  if (it == g_heap.end())
    return "";
  for (auto &v : it->second)
    if (is_visible(v, snap, xid))
      return v.value;
  return "";
}

void mvcc_insert(const RowKey &key, const string &value, TxID xid)
{
  lock_guard<mutex> lk(g_heap_mutex);
  g_heap[key].push_front({value, xid, 0});
}

void mvcc_update(const RowKey &key, const string &new_value, TxID xid)
{
  lock_guard<mutex> lk(g_heap_mutex);
  TxID snap = snapshot_of(xid);
  auto it = g_heap.find(key);
  if (it != g_heap.end())
  {
    for (auto &v : it->second)
    {
      if (is_visible(v, snap, xid) && v.xmax == 0)
      {
        v.xmax = xid;
        break;
      }
    }
  }
  g_heap[key].push_front({new_value, xid, 0});
}

void mvcc_delete(const RowKey &key, TxID xid)
{
  lock_guard<mutex> lk(g_heap_mutex);
  TxID snap = snapshot_of(xid);
  auto it = g_heap.find(key);
  if (it == g_heap.end())
    return;
  for (auto &v : it->second)
  {
    if (is_visible(v, snap, xid) && v.xmax == 0)
    {
      v.xmax = xid;
      return;
    }
  }
}

enum class LockMode
{
  SHARED,
  EXCLUSIVE
};

struct LockRequest
{
  TxID xid;
  LockMode mode;
  bool granted = false;
};

static mutex g_lm_mutex;
static condition_variable g_lock_cv;
static unordered_map<RowKey, list<LockRequest>> g_lock_table;
static unordered_map<TxID, unordered_set<TxID>> g_waits_for;

bool has_cycle(TxID start, const unordered_map<TxID, unordered_set<TxID>> &graph)
{
  unordered_set<TxID> visited, on_stack;
  function<bool(TxID)> dfs = [&](TxID node) -> bool
  {
    visited.insert(node);
    on_stack.insert(node);
    auto it = graph.find(node);
    if (it != graph.end())
    {
      for (TxID nb : it->second)
      {
        if (on_stack.count(nb))
          return true;
        if (!visited.count(nb) && dfs(nb))
          return true;
      }
    }
    on_stack.erase(node);
    return false;
  };
  return dfs(start);
}

class DeadlockException : public runtime_error
{
public:
  explicit DeadlockException(TxID xid)
      : runtime_error("Deadlock detected, aborting tx " + to_string(xid)) {}
};

void acquire_lock(const RowKey &key, TxID xid, LockMode mode)
{
  unique_lock<mutex> ul(g_lm_mutex);

  {
    lock_guard<mutex> lk(g_tx_mutex);
    if (g_transactions.at(xid).in_shrinking)
      throw runtime_error("2PL violation: cannot acquire lock in shrinking phase");
  }

  auto &q = g_lock_table[key];

  for (auto &r : q)
  {
    if (r.xid == xid && r.granted)
    {
      if (mode == LockMode::SHARED)
        return;
      if (r.mode == LockMode::EXCLUSIVE)
        return;
    }
  }

  q.push_back({xid, mode, false});
  LockRequest *my_req = &q.back();

  while (true)
  {
    bool conflict = false;
    unordered_set<TxID> blocking;

    for (auto &r : q)
    {
      if (&r == my_req)
        break;
      if (!r.granted || r.xid == xid)
        continue;
      if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE)
      {
        conflict = true;
        blocking.insert(r.xid);
      }
    }

    if (!conflict)
    {
      my_req->granted = true;
      g_waits_for.erase(xid);
      return;
    }

    g_waits_for[xid] = blocking;
    if (has_cycle(xid, g_waits_for))
    {
      g_waits_for.erase(xid);
      q.remove_if([&](const LockRequest &r)
                  { return r.xid == xid && !r.granted; });
      throw DeadlockException(xid);
    }

    g_lock_cv.wait(ul);
  }
}

void release_locks(TxID xid)
{
  {
    lock_guard<mutex> lk(g_tx_mutex);
    if (g_transactions.count(xid))
      g_transactions.at(xid).in_shrinking = true;
  }
  lock_guard<mutex> ul(g_lm_mutex);
  for (auto &[key, q] : g_lock_table)
    q.remove_if([&](const LockRequest &r)
                { return r.xid == xid; });
  g_waits_for.erase(xid);
  g_lock_cv.notify_all();
}

class TransactionManager
{
public:
  TxID begin() { return begin_transaction(); }

  string read(TxID xid, const RowKey &key)
  {
    acquire_lock(key, xid, LockMode::SHARED);
    return mvcc_read(key, xid);
  }

  void insert(TxID xid, const RowKey &key, const string &value)
  {
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    mvcc_insert(key, value, xid);
  }

  void update(TxID xid, const RowKey &key, const string &value)
  {
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    mvcc_update(key, value, xid);
  }

  void del(TxID xid, const RowKey &key)
  {
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    mvcc_delete(key, xid);
  }

  void commit(TxID xid)
  {
    {
      lock_guard<mutex> lk(g_tx_mutex);
      g_transactions.at(xid).status = TxStatus::COMMITTED;
    }
    release_locks(xid);
    cout << "[TX " << xid << "] COMMITTED\n";
  }

  void abort(TxID xid)
  {
    {
      lock_guard<mutex> lk(g_heap_mutex);
      for (auto &[key, chain] : g_heap)
      {
        for (auto &v : chain)
        {
          if (v.xmin == xid)
            v.xmax = xid;
          if (v.xmax == xid)
            v.xmax = 0;
        }
      }
    }
    {
      lock_guard<mutex> lk(g_tx_mutex);
      g_transactions.at(xid).status = TxStatus::ABORTED;
    }
    release_locks(xid);
    cout << "[TX " << xid << "] ABORTED\n";
  }
};

void print_val(const string &v, TxID xid, const RowKey &key)
{
  cout << "  [TX " << xid << "] READ " << key << " = "
       << (v.empty() ? "<not visible>" : v) << "\n";
}

int main()
{
  TransactionManager tm;

  cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
  {
    TxID t1 = tm.begin();
    tm.insert(t1, "balance", "1000");
    tm.commit(t1);

    TxID t2 = tm.begin();
    TxID t3 = tm.begin();
    tm.update(t3, "balance", "2000");
    tm.commit(t3);

    print_val(tm.read(t2, "balance"), t2, "balance");
    tm.commit(t2);
  }

  cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
  {
    TxID t4 = tm.begin();
    TxID t5 = tm.begin();
    print_val(tm.read(t4, "balance"), t4, "balance");
    print_val(tm.read(t5, "balance"), t5, "balance");
    tm.commit(t4);
    tm.commit(t5);
  }

  cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
  {
    TxID t6 = tm.begin();
    tm.update(t6, "balance", "3000");

    thread reader([&]()
                  {
            TxID t7 = tm.begin();
            cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            print_val(tm.read(t7, "balance"), t7, "balance");
            tm.commit(t7); });

    this_thread::sleep_for(chrono::milliseconds(50));
    tm.commit(t6);
    reader.join();
  }

  cout << "\n=== Scenario 4: Deadlock Detection ===\n";
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

    thread th1([&]()
               {
            try {
                acquire_lock("B", t8, LockMode::EXCLUSIVE);
                tm.commit(t8);
            } catch (DeadlockException &e) {
                cout << "  " << e.what() << "\n";
                tm.abort(t8);
            } });

    this_thread::sleep_for(chrono::milliseconds(20));

    try
    {
      acquire_lock("A", t9, LockMode::EXCLUSIVE);
      tm.commit(t9);
    }
    catch (DeadlockException &e)
    {
      cout << "  " << e.what() << "\n";
      tm.abort(t9);
    }

    th1.join();
  }

  cout << "\nAll scenarios complete.\n";
  return 0;
}
