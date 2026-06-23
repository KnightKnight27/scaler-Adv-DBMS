#pragma once

#include <atomic>
#include <cstdint>

namespace minidb {

using namespace std;

using TxnId = int64_t;

constexpr TxnId INVALID_TXN_ID = -1;

inline TxnId NextTxnId() {
  static atomic<TxnId> counter{1};
  return counter.fetch_add(1);
}

} // namespace minidb