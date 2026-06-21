#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "catalog/value.h"

namespace walterdb {

// A single column definition.  `primary_key` marks the (single) column whose
// value is the row's identity and the key of the table's primary B+tree index.
struct Column {
  std::string name;
  TypeId type;
  bool primary_key = false;
};

// ---------------------------------------------------------------------------
// Schema -- the ordered list of columns for a table.  Tuples are encoded and
// decoded against a Schema, and the executor resolves column references
// (by name) to positional indices through it.
// ---------------------------------------------------------------------------

class Schema {
 public:
  Schema() = default;
  explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}

  size_t num_columns() const { return columns_.size(); }
  const Column& column(size_t i) const { return columns_[i]; }
  const std::vector<Column>& columns() const { return columns_; }

  // Index of a column by name, or nullopt if absent.  Case-insensitive.
  std::optional<size_t> index_of(const std::string& name) const;

  // Index of the primary-key column, or nullopt if none was declared.
  std::optional<size_t> primary_key_index() const;

 private:
  std::vector<Column> columns_;
};

}  // namespace walterdb
