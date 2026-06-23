#pragma once
#include <string>
#include <vector>
#include "common/types.h"

namespace minidb {

// One column of a table.
struct Column {
  std::string name;
  TypeId      type;
  bool        is_primary_key{false};
};

// A table schema: an ordered list of columns. Exactly one column may be the
// primary key (its position drives the B+ tree / LSM key).
class Schema {
 public:
  Schema() = default;
  explicit Schema(std::vector<Column> cols) : columns_(std::move(cols)) {
    for (size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i].is_primary_key) pk_index_ = static_cast<int>(i);
    }
  }

  const std::vector<Column> &columns() const { return columns_; }
  size_t num_columns() const { return columns_.size(); }
  const Column &column(size_t i) const { return columns_[i]; }

  // Index of the primary-key column, or -1 if none.
  int pk_index() const { return pk_index_; }

  // Returns column position by name, or -1 if not found.
  int GetColumnIndex(const std::string &name) const {
    for (size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i].name == name) return static_cast<int>(i);
    }
    return -1;
  }

 private:
  std::vector<Column> columns_;
  int                 pk_index_{-1};
};

}  // namespace minidb
