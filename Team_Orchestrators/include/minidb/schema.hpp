#pragma once
// Table schema: ordered columns with names and types.
#include "minidb/types.hpp"
#include <string>
#include <vector>

namespace minidb {

struct Column {
  std::string name;
  TypeId type;
  bool primary_key = false;
};

class Schema {
 public:
  Schema() = default;
  explicit Schema(std::vector<Column> cols) : cols_(std::move(cols)) {}

  const std::vector<Column>& columns() const { return cols_; }
  size_t size() const { return cols_.size(); }
  const Column& column(size_t i) const { return cols_[i]; }
  static constexpr size_t npos = static_cast<size_t>(-1);

  // Returns the index of a column by name, or npos if absent.
  size_t index_of(const std::string& name) const {
    for (size_t i = 0; i < cols_.size(); ++i)
      if (cols_[i].name == name) return i;
    return npos;
  }

  // Index of the primary-key column, if any.
  size_t primary_key_index() const {
    for (size_t i = 0; i < cols_.size(); ++i)
      if (cols_[i].primary_key) return i;
    return npos;
  }

 private:
  std::vector<Column> cols_;
};

}  // namespace minidb
