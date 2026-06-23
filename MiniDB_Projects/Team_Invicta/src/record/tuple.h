#pragma once
#include <cstring>
#include <string>
#include <vector>
#include "common/types.h"
#include "record/schema.h"

namespace minidb {

// A materialized row: an ordered list of Values. A Tuple is laid out in storage
// as a self-contained byte string, decoded against a Schema:
//   INTEGER  -> 8 bytes (host-endian int64)
//   VARCHAR  -> 4-byte length prefix + raw bytes
class Tuple {
 public:
  Tuple() = default;
  explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}

  const std::vector<Value> &values() const { return values_; }
  const Value &value(size_t i) const { return values_[i]; }
  size_t size() const { return values_.size(); }
  bool empty() const { return values_.empty(); }

  // Serialize this tuple's values according to `schema` column types.
  std::string Serialize(const Schema &schema) const {
    std::string out;
    for (size_t i = 0; i < schema.num_columns(); ++i) {
      const Value &v = values_[i];
      if (schema.column(i).type == TypeId::INTEGER) {
        int64_t x = v.i;
        out.append(reinterpret_cast<const char *>(&x), sizeof(x));
      } else {  // VARCHAR
        uint32_t len = static_cast<uint32_t>(v.s.size());
        out.append(reinterpret_cast<const char *>(&len), sizeof(len));
        out.append(v.s);
      }
    }
    return out;
  }

  // Decode a tuple of `schema` shape from raw bytes.
  static Tuple Deserialize(const Schema &schema, const char *data, size_t len) {
    std::vector<Value> vals;
    size_t off = 0;
    for (size_t i = 0; i < schema.num_columns(); ++i) {
      if (schema.column(i).type == TypeId::INTEGER) {
        int64_t x = 0;
        std::memcpy(&x, data + off, sizeof(x));
        off += sizeof(x);
        vals.push_back(Value::Int(x));
      } else {  // VARCHAR
        uint32_t slen = 0;
        std::memcpy(&slen, data + off, sizeof(slen));
        off += sizeof(slen);
        vals.push_back(Value::Str(std::string(data + off, slen)));
        off += slen;
      }
    }
    (void)len;
    return Tuple(std::move(vals));
  }

  static Tuple Deserialize(const Schema &schema, const std::string &bytes) {
    return Deserialize(schema, bytes.data(), bytes.size());
  }

  // Primary-key value of this tuple under `schema` (caller ensures pk exists).
  int64_t PrimaryKey(const Schema &schema) const {
    return values_[schema.pk_index()].i;
  }

 private:
  std::vector<Value> values_;
};

}  // namespace minidb
