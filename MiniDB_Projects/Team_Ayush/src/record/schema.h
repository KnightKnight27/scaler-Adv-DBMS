#pragma once
#include <string>
#include <vector>
#include "common/types.h"

namespace minidb {

// A column definition. For INT, `length` is 4. For VARCHAR(n), `length` is n
// and the stored field is fixed-width (zero padded) -- this is what keeps every
// tuple fixed-length.
struct Column {
  std::string name;
  ValueType   type;
  int         length;

  Column() : type(ValueType::INT), length(4) {}
  Column(const std::string& n, ValueType t, int len)
      : name(n), type(t), length(len) {}

  static Column Int(const std::string& n) { return Column(n, ValueType::INT, 4); }
  static Column Varchar(const std::string& n, int n_chars) {
    return Column(n, ValueType::VARCHAR, n_chars);
  }
};

// A table schema: an ordered list of columns plus the index of the primary-key
// column (-1 if none).
struct Schema {
  std::vector<Column> columns;
  int pk_index = -1;

  // Fixed size, in bytes, of one serialized tuple for this schema.
  int RecordSize() const {
    int total = 0;
    for (const Column& c : columns) total += c.length;
    return total;
  }

  // Find a column's position by name, or -1.
  int ColumnIndex(const std::string& name) const {
    for (size_t i = 0; i < columns.size(); ++i) {
      if (columns[i].name == name) return static_cast<int>(i);
    }
    return -1;
  }
};

}  // namespace minidb
