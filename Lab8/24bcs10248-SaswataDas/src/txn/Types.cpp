#include "txn/Types.h"

#include "txn/Transaction.h"

namespace txn {

const char* toString(LockMode mode) {
    switch (mode) {
        case LockMode::Shared: return "S";
        case LockMode::Exclusive: return "X";
    }
    return "?";
}

const char* toString(OpStatus status) {
    switch (status) {
        case OpStatus::Ok: return "Ok";
        case OpStatus::Blocked: return "Blocked";
        case OpStatus::Aborted: return "Aborted";
    }
    return "?";
}

const char* toString(TxnState state) {
    switch (state) {
        case TxnState::Active: return "Active";
        case TxnState::Committed: return "Committed";
        case TxnState::Aborted: return "Aborted";
    }
    return "?";
}

}  // namespace txn
