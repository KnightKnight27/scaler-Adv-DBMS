#pragma once
#include <string>
#include <cstdint>
#include <limits>

using TxnId     = uint32_t;
using Timestamp = uint64_t;
using RecordKey = std::string;

constexpr Timestamp INF_TS     = std::numeric_limits<Timestamp>::max();
constexpr TxnId     INVALID_TXN = 0;

enum class LockMode { SHARED, EXCLUSIVE };
enum class TxnState { ACTIVE, COMMITTED, ABORTED };

struct Version {
    std::string value;
    TxnId       created_by;
    Timestamp   begin_ts;
    Timestamp   end_ts;   // INF_TS means "still current"
};
