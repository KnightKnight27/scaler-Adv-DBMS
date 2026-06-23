#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

// Core value types shared across every layer of MiniDB.
namespace minidb {

using Key    = int64_t;      // primary key (we only support INT primary keys)
using Bytes  = std::string;  // a serialized row; opaque to the storage engines
using PageId = int32_t;      // index of a page within the data file
using TxnId  = uint64_t;     // transaction identifier
using TableId = uint32_t;    // identifies a table for locking / logging

constexpr PageId kInvalidPage = -1;

// Physical location of a row inside a heap file: which page, which slot.
struct RID {
  PageId   page_id = kInvalidPage;
  uint16_t slot    = 0;
};

enum class ColumnType { Int, Text };

struct Column {
  std::string name;
  ColumnType  type;
};

// A single column value: either an integer or text.
using Value = std::variant<int64_t, std::string>;

// A row is a positional list of values, matching the table's Schema order.
using Row = std::vector<Value>;

// Describes the columns of one table.
struct Schema {
  std::vector<Column> columns;

  std::size_t size() const { return columns.size(); }

  // Returns the position of a column by name, or -1 if it does not exist.
  int index_of(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
      if (columns[i].name == name) return i;
    }
    return -1;
  }
};

// Lightweight per-table statistics the optimizer reads to estimate costs.
struct TableStats {
  int64_t row_count = 0;
  Key     min_key   = 0;
  Key     max_key   = 0;
  bool    has_index = false;
};

}  // namespace minidb
