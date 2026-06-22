#pragma once
#include <string>
#include <vector>
#include "common/types.h"

namespace minidb {

struct Column {
  std::string name;
  TypeId type;
};

// A table's logical layout: an ordered list of columns plus which column (if
// any) is the primary key. The primary key column drives the B+ tree index.
class Schema {
 public:
  Schema() = default;
  explicit Schema(std::vector<Column> cols, int pk_index = -1)
      : columns_(std::move(cols)), pk_index_(pk_index) {}

  const std::vector<Column>& Columns() const { return columns_; }
  size_t ColumnCount() const { return columns_.size(); }
  const Column& GetColumn(size_t i) const { return columns_[i]; }

  // Index of a column by name, or -1 if absent.
  int ColIndex(const std::string& name) const {
    for (size_t i = 0; i < columns_.size(); i++)
      if (columns_[i].name == name) return static_cast<int>(i);
    return -1;
  }

  int PkIndex() const { return pk_index_; }
  bool HasPk() const { return pk_index_ >= 0; }

 private:
  std::vector<Column> columns_;
  int pk_index_ = -1;
};

}  // namespace minidb
