#pragma once

#include "Types.h"

namespace txn {

enum class TxnState { Active, Committed, Aborted };

// Per-transaction bookkeeping owned by the TransactionManager.
struct Transaction {
    TxnId id = kInvalidTxn;
    Timestamp startTs = 0;   // snapshot timestamp captured at begin(); drives MVCC visibility
    Timestamp commitTs = 0;  // assigned at commit
    TxnState state = TxnState::Active;
    bool readOnly = false;   // read-only transactions read from their snapshot without locking
};

const char* toString(TxnState state);

}  // namespace txn
