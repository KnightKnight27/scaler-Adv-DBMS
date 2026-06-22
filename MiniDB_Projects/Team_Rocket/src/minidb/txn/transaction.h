#pragma once

#include <cstdint>

namespace minidb {

enum class TxnState { Active, Committed, Aborted };

struct Transaction {
    int32_t id = 0;
    TxnState state = TxnState::Active;
    bool autocommit = false;  // an implicit single-statement transaction
};

}  // namespace minidb
