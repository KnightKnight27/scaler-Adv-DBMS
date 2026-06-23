#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/value.h"

namespace minidb {

// One attribute of a relation. `length` is the max byte length for VARCHAR;
// INTEGER is always 4 bytes.
struct Column {
  std::string name;
  TypeId type;
  uint32_t length;  // VARCHAR max length (0 for INTEGER)

  Column(std::string n, TypeId t, uint32_t len = 0)
      : name(std::move(n)), type(t), length(t == TypeId::INTEGER ? 4 : len) {}
};

// An ordered list of columns. Tuples are serialized/deserialized against a
// schema, and the binder resolves column names to indices through it.
class Schema {
 public:
  Schema() = default;
  explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}

  size_t GetColumnCount() const { return columns_.size(); }
  const Column &GetColumn(size_t i) const { return columns_[i]; }
  const std::vector<Column> &GetColumns() const { return columns_; }

  // Index of a column by name, or -1 if absent.
  int GetColIdx(const std::string &name) const {
    for (size_t i = 0; i < columns_.size(); i++) {
      if (columns_[i].name == name) return static_cast<int>(i);
    }
    return -1;
  }

 private:
  std::vector<Column> columns_;
};

}  // namespace minidb
