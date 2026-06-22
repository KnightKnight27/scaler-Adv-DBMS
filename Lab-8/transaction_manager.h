#pragma once
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#if __has_include(<optional>)
#include <optional>
#else
#include <experimental/optional>
namespace std
{
    template <typename T>
    using optional = std::experimental::optional<T>;
    constexpr auto nullopt = std::experimental::nullopt;
}
#endif
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using TxID = std::uint64_t;
using RowKey = std::string;

enum class TxStatus
{
    ACTIVE,
    COMMITTED,
    ABORTED
};
enum class LockMode
{
    SHARED,
    EXCLUSIVE
};

struct Transaction
{
    TxID id = 0;
    TxID snapshot_xid = 0;
    TxStatus status = TxStatus::ACTIVE;
    bool in_shrinking = false;
};
struct RowVersion
{
    std::string value;
    TxID xmin = 0;
    TxID xmax = 0;
};
struct LockRequest
{
    TxID xid;
    LockMode mode;
    bool granted = false;
};

struct LockQueue
{
    std::list<LockRequest> requests;
    std::mutex mu;
    std::condition_variable cv;
};

class DeadlockException : public std::runtime_error
{
public:
    explicit DeadlockException(TxID xid);
};

class TransactionManager
{
public:
    TxID begin();
    std::optional<std::string> read(TxID xid, const RowKey &key);
    void insert(TxID xid, const RowKey &key, const std::string &value);
    void update(TxID xid, const RowKey &key, const std::string &value);
    void remove(TxID xid, const RowKey &key);
    void commit(TxID xid);
    void abort(TxID xid);
    void acquire_lock(const RowKey &key, TxID xid, LockMode mode);

private:
    // ── Transaction table ──
    std::atomic<TxID> next_xid_{1};
    std::mutex tx_mutex_;
    std::unordered_map<TxID, Transaction> transactions_;

    // ── MVCC heap: key → version chain (newest first) ──
    std::mutex heap_mutex_;
    std::unordered_map<RowKey, std::list<RowVersion>> heap_;

    // ── Lock manager ──
    std::mutex lm_mutex_;
    std::unordered_map<RowKey, LockQueue> lock_table_;
    std::unordered_map<TxID, std::unordered_set<TxID>> waits_for_;

    bool is_committed(TxID xid);
    bool is_aborted(TxID xid);
    bool is_visible(const RowVersion &v, TxID snapshot_xid, TxID reader_xid);
    bool has_cycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>> &graph);
    void release_locks(TxID xid);
};
