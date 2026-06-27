#pragma once
#include <string>
#include <optional>
#include <cstdint>
#include <stdexcept>

using TxID = uint64_t;

enum class LockMode { SHARED, EXCLUSIVE };

class TransactionManager {
public:
    TxID begin();
    void commit(TxID xid);
    void abort(TxID xid);

    void acquire_lock(const std::string& resource, TxID xid, LockMode mode);
    void release_locks(TxID xid);

    void        mvcc_write(TxID xid, const std::string& key, const std::string& value);
    void        mvcc_delete(TxID xid, const std::string& key);
    std::optional<std::string> mvcc_read(TxID xid, const std::string& key);
};

struct DeadlockException : std::runtime_error {
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock — aborting tx " + std::to_string(xid)) {}
};
