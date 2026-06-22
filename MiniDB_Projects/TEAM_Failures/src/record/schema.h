// ============================================================================
// schema.h  --  Describes the shape of a table: its ordered list of columns,
// each with a name and a type.  The schema is the contract that lets us turn
// raw bytes on a page back into typed Values (and vice-versa).
// ============================================================================
#pragma once

#include "common/common.h"
#include "record/value.h"

namespace minidb {

struct Column {
  string name;
  TypeId      type;
};

class Schema {
 public:
  Schema() = default;
  explicit Schema(vector<Column> cols) : columns_(move(cols)) {}

  const vector<Column> &columns() const { return columns_; }
  size_t size() const { return columns_.size(); }
  const Column &column(size_t i) const { return columns_.at(i); }

  // Returns the position of a column by name, or -1 if absent.  Used by the
  // binder/executor to resolve "WHERE age > 30" to a column index.
  int getColIdx(const string &name) const {
    for (size_t i = 0; i < columns_.size(); ++i)
      if (columns_[i].name == name) return static_cast<int>(i);
    return -1;
  }

  string toString() const {
    string s = "(";
    for (size_t i = 0; i < columns_.size(); ++i) {
      if (i) s += ", ";
      s += columns_[i].name + " " + TypeToString(columns_[i].type);
    }
    return s + ")";
  }

 private:
  vector<Column> columns_;
};

}  // namespace minidb
