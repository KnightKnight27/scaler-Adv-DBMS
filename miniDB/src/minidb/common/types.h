#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace minidb {

using PageId = std::uint32_t;
using SlotId = std::uint16_t;
using TxnId = std::uint64_t;
using Timestamp = std::uint64_t;

constexpr std::size_t kPageSize = 4096;
constexpr PageId kInvalidPageId = UINT32_MAX;

struct Rid {
  PageId page_id{kInvalidPageId};
  SlotId slot_id{0};

  bool operator==(const Rid& other) const = default;
  bool IsValid() const { return page_id != kInvalidPageId; }
  std::string ToString() const;
};

enum class ColumnType { Int, Text };

struct Column {
  std::string name;
  ColumnType type{ColumnType::Text};
  bool primary_key{false};
};

using Value = std::string;
using Row = std::vector<Value>;

struct Predicate {
  std::string column;
  std::string op;
  Value value;
  bool empty{true};
};

std::string Trim(std::string_view input);
std::string ToUpper(std::string_view input);
std::vector<std::string> SplitCsv(std::string_view input);
std::string EncodeRow(const Row& row);
Row DecodeRow(std::string_view encoded);
std::string Escape(Value value);
Value Unescape(std::string_view value);

class MiniDbError : public std::runtime_error {
 public:
  explicit MiniDbError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace minidb
