// ============================================================================
// tuple.h  --  A tuple (row) is an ordered list of Values matching some Schema.
//
// In memory a Tuple is just vector<Value> -- easy to work with.  On disk a
// tuple is a flat byte string.  serialize()/deserialize() convert between the
// two using the schema.  The byte layout is:
//
//   for each column, in schema order:
//     INTEGER : 4 bytes, little-endian
//     VARCHAR : 4-byte length prefix, then that many raw bytes
//
// This is a self-describing-given-the-schema format: you cannot decode the
// bytes without the schema, which is exactly why every table carries one.
// ============================================================================
#pragma once

#include "common/common.h"
#include "record/schema.h"
#include "record/value.h"

namespace minidb {

class Tuple {
 public:
  Tuple() = default;
  explicit Tuple(vector<Value> values) : values_(move(values)) {}

  const vector<Value> &values() const { return values_; }
  const Value &value(size_t i) const { return values_.at(i); }
  size_t size() const { return values_.size(); }

  // ---- Encode this tuple to bytes using the given schema ----
  string serialize(const Schema &schema) const {
    string out;
    for (size_t i = 0; i < schema.size(); ++i) {
      const Value &v = values_.at(i);
      if (schema.column(i).type == TypeId::INTEGER) {
        int32_t x = v.asInt();
        out.append(reinterpret_cast<const char *>(&x), sizeof(x));
      } else {
        const string &s = v.asString();
        int32_t len = static_cast<int32_t>(s.size());
        out.append(reinterpret_cast<const char *>(&len), sizeof(len));
        out.append(s);
      }
    }
    return out;
  }

  // ---- Decode bytes back into a Tuple using the given schema ----
  static Tuple deserialize(const char *data, const Schema &schema) {
    vector<Value> vals;
    size_t off = 0;
    for (size_t i = 0; i < schema.size(); ++i) {
      if (schema.column(i).type == TypeId::INTEGER) {
        int32_t x;
        memcpy(&x, data + off, sizeof(x));
        off += sizeof(x);
        vals.emplace_back(x);
      } else {
        int32_t len;
        memcpy(&len, data + off, sizeof(len));
        off += sizeof(len);
        vals.emplace_back(string(data + off, len));
        off += len;
      }
    }
    return Tuple(move(vals));
  }

  string toString() const {
    string s = "(";
    for (size_t i = 0; i < values_.size(); ++i) {
      if (i) s += ", ";
      s += values_[i].toString();
    }
    return s + ")";
  }

 private:
  vector<Value> values_;
};

}  // namespace minidb
