#pragma once

#include <string>

#include "common/exception.h"
#include "common/types.h"

namespace minidb {

enum class LockMode { SHARED, EXCLUSIVE };
enum class TxState { ACTIVE, COMMITTED, ABORTED };

// Thrown by LockManager::acquire when granting the lock would close a cycle in
// the waits-for graph. Carries the victim transaction id (the one aborted).
class DeadlockException : public DBException {
public:
    explicit DeadlockException(TxId victim)
        : DBException("deadlock detected; aborting transaction " + std::to_string(victim)),
          victim(victim) {}
    TxId victim;
};

struct Transaction {
    TxId    id    = INVALID_TXID;
    TxState state = TxState::ACTIVE;
};

} // namespace minidb
