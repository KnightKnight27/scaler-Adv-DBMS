#pragma once

#include <cstdint>
#include <vector>

#include "catalog/schema.h"
#include "common/rid.h"
#include "common/value.h"

namespace minidb {

// A serialized row. The on-disk layout, column by column, is:
//   INTEGER : 4 raw bytes
//   VARCHAR : 4-byte length prefix, then that many bytes
// The schema (not the tuple) knows the column types, so a Tuple is just a byte
// blob plus the RID identifying where it lives on disk.
class Tuple {
 public:
  Tuple() = default;
  Tuple(const std::vector<Value> &values, const Schema &schema);

  // Decode one column / all columns against a schema.
  Value GetValue(const Schema &schema, size_t col_idx) const;
  std::vector<Value> GetValues(const Schema &schema) const;

  const char *Data() const { return data_.data(); }
  uint32_t Size() const { return static_cast<uint32_t>(data_.size()); }
  void SetData(const char *src, uint32_t len) { data_.assign(src, src + len); }
  bool IsValid() const { return !data_.empty(); }

  RID GetRid() const { return rid_; }
  void SetRid(RID rid) { rid_ = rid; }

 private:
  std::vector<char> data_;
  RID rid_;
};

}  // namespace minidb
