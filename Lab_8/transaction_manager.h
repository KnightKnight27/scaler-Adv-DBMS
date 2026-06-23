#pragma once
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#if defined(__cplusplus) && __cplusplus >= 201703L
#include <optional>
#else
#include <utility>
namespace std {
    struct nullopt_t {
        struct init {};
        constexpr explicit nullopt_t(init) {}
    };
    constexpr nullopt_t nullopt{nullopt_t::init{}};

    template <typename T>
    class optional {
        bool has_val;
        T val;
    public:
        optional() : has_val(false) {}
        optional(nullopt_t) : has_val(false) {}
        optional(const T& v) : has_val(true), val(v) {}
        optional(T&& v) : has_val(true), val(std::move(v)) {}
        bool has_value() const { return has_val; }
        const T& value() const { return val; }
        const T& operator*() const { return val; }
        const T* operator->() const { return &val; }
        explicit operator bool() const { return has_val; }
    };
}
#endif
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using TxnID = std::uint64_t;
using DbKey = std::string;

enum class TransactionState
{
    RUNNING,
    SUCCESS,
    ROLLED_BACK
};

enum class LockType
{
    READ_LOCK,
    WRITE_LOCK
};

struct TxContext
{
    TxnID txId = 0;
    TxnID snapId = 0;
    TransactionState state = TransactionState::RUNNING;
    bool shrinkingPhase = false;

    TxContext() = default;
    TxContext(TxnID id, TxnID snap, TransactionState st, bool shrinking)
        : txId(id), snapId(snap), state(st), shrinkingPhase(shrinking) {}
};

struct DbVersion
{
    std::string content;
    TxnID createdTx = 0;
    TxnID deletedTx = 0;

    DbVersion() = default;
    DbVersion(const std::string& val, TxnID created, TxnID deleted)
        : content(val), createdTx(created), deletedTx(deleted) {}
};

struct LockReq
{
    TxnID txnId;
    LockType lockMode;
    bool isGranted = false;

    LockReq(TxnID id, LockType mode, bool granted)
        : txnId(id), lockMode(mode), isGranted(granted) {}
};

struct LockWaitQueue
{
    std::list<LockReq> reqs;
    std::mutex mtx;
    std::condition_variable cvWait;
};

class CycleDetectedException : public std::runtime_error
{
public:
    explicit CycleDetectedException(TxnID txId);
};

class DbTransactionEngine
{
public:
    TxnID startTransaction();
    std::optional<std::string> readKey(TxnID txId, const DbKey &key);
    void insertKey(TxnID txId, const DbKey &key, const std::string &value);
    void updateKey(TxnID txId, const DbKey &key, const std::string &value);
    void deleteKey(TxnID txId, const DbKey &key);
    void commitTransaction(TxnID txId);
    void rollbackTransaction(TxnID txId);
    void requestLock(const DbKey &key, TxnID txId, LockType mode);

private:
    // ── Transaction table ──
    std::atomic<TxnID> nextTxId_{1};
    std::mutex txnMutex_;
    std::unordered_map<TxnID, TxContext> txnMap_;

    // ── MVCC heap: key → version chain (newest first) ──
    std::mutex storageMutex_;
    std::unordered_map<DbKey, std::list<DbVersion>> dbStorage_;

    // ── Lock manager ──
    std::mutex lmMutex_;
    std::unordered_map<DbKey, LockWaitQueue> lockTable_;
    std::unordered_map<TxnID, std::unordered_set<TxnID>> dependencyGraph_;

    bool checkCommitted(TxnID txId);
    bool checkAborted(TxnID txId);
    bool checkVisibility(const DbVersion &v, TxnID snapId, TxnID readerId);
    bool detectCycle(TxnID start, const std::unordered_map<TxnID, std::unordered_set<TxnID>> &graph);
    void releaseAllLocks(TxnID txId);
};