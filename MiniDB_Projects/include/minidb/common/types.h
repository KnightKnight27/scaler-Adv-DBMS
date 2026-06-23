#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

namespace minidb {

using PageId = std::int32_t;
using SlotId = std::uint16_t;
using TxnId = std::uint64_t;
using Lsn = std::uint64_t;

inline constexpr std::size_t kPageSize = 4096;
inline constexpr PageId kInvalidPageId = -1;

struct RID {
  PageId page_id{kInvalidPageId};
  SlotId slot_id{0};
  auto operator<=>(const RID &) const = default;
};

inline std::ostream &operator<<(std::ostream &out, const RID &rid) {
  return out << "RID(" << rid.page_id << ',' << rid.slot_id << ')';
}

struct Record {
  int key{};
  std::string value;
};

enum class LogType : std::uint8_t { Begin, Insert, Delete, Commit, Abort };

}  // namespace minidb
