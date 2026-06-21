#pragma once

#include <string>
#include <vector>

#include "catalog/schema.h"
#include "catalog/value.h"

namespace walterdb {

// ---------------------------------------------------------------------------
// Tuple -- one row: a positional vector of Values matching a Schema.
//
// Serialization is schema-driven (the schema tells the decoder how to read each
// field), which keeps the stored bytes compact -- no per-value type tags:
//
//   [ null bitmap : ceil(ncols/8) bytes ]
//   for each column whose null bit is 0, in column order:
//       Integer -> 8 bytes little-endian
//       Double  -> 8 bytes
//       Boolean -> 1 byte
//       Varchar -> u32 length + raw bytes
//
// This is the project's "byte-level record encoding" deliverable.
// ---------------------------------------------------------------------------

class Tuple {
 public:
  Tuple() = default;
  explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}

  size_t size() const { return values_.size(); }
  const Value& value(size_t i) const { return values_[i]; }
  const std::vector<Value>& values() const { return values_; }

  // Encode this tuple's bytes according to `schema` (must match arity/types).
  std::string encode(const Schema& schema) const;

  // Decode bytes produced by encode() back into a Tuple given the same schema.
  static Tuple decode(const Schema& schema, std::string_view bytes);

  std::string to_string() const;  // "(a, b, c)" for display

 private:
  std::vector<Value> values_;
};

}  // namespace walterdb
