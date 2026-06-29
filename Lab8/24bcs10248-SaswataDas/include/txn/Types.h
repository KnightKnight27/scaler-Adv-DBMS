#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace txn {

using TxnId = std::uint64_t;
using Timestamp = std::uint64_t;
using Key = std::string;
using Value = int;

constexpr TxnId kInvalidTxn = 0;
constexpr Timestamp kUncommitted = 0;                                  // version not yet committed
constexpr Timestamp kInfinity = std::numeric_limits<Timestamp>::max(); // version never superseded

// Lock modes for Strict Two-Phase Locking.
enum class LockMode { Shared, Exclusive };

// Outcome of a data operation submitted to the TransactionManager.
//   Ok      - operation completed.
//   Blocked - the lock could not be granted; the transaction is now waiting
//             (no deadlock was involved). The caller may retry later.
//   Aborted - this transaction was chosen as a deadlock victim and rolled
//             back; it is no longer usable.
enum class OpStatus { Ok, Blocked, Aborted };

struct ReadResult {
    OpStatus status = OpStatus::Ok;
    std::optional<Value> value;  // populated only when status == Ok and the key exists
};

const char* toString(LockMode mode);
const char* toString(OpStatus status);

}  // namespace txn
