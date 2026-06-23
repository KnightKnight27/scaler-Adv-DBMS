#pragma once
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "value.h"

namespace minidb {

struct Column {
  std::string name;
  TypeId type;
  bool is_primary_key = false;
};

class Schema {
 public:
  Schema() = default;
  explicit Schema(std::vector<Column> cols) : columns_(std::move(cols)) {}

  const std::vector<Column> &Columns() const { return columns_; }
  size_t NumColumns() const { return columns_.size(); }

  std::optional<int> ColumnIndex(const std::string &name) const {
    for (size_t i = 0; i < columns_.size(); i++)
      if (columns_[i].name == name) return static_cast<int>(i);
    return std::nullopt;
  }

  int PrimaryKeyIndex() const {
    for (size_t i = 0; i < columns_.size(); i++)
      if (columns_[i].is_primary_key) return static_cast<int>(i);
    return -1;
  }

 private:
  std::vector<Column> columns_;
};

// Serialized tuple format (fixed layout driven by the schema, values
// written in column order): VARCHAR is length-prefixed, INTEGER is 8 bytes.
class Tuple {
 public:
  Tuple() = default;
  explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}

  const std::vector<Value> &Values() const { return values_; }
  const Value &Get(int idx) const { return values_[idx]; }

  std::string Serialize() const {
    std::string out;
    for (const auto &v : values_) {
      if (v.GetType() == TypeId::INTEGER) {
        int64_t x = v.AsInt();
        out.append(reinterpret_cast<const char *>(&x), sizeof(x));
      } else {
        const std::string &s = v.AsStr();
        int32_t len = static_cast<int32_t>(s.size());
        out.append(reinterpret_cast<const char *>(&len), sizeof(len));
        out.append(s);
      }
    }
    return out;
  }

  // Bounds-checked: a VARCHAR length field that's corrupt or (pre-fix)
  // produced by inserting a value of the wrong type used to be trusted as
  //-is, letting `offset` jump arbitrarily far past `data` and turning the
  // next column's read into an out-of-bounds memory access. Every read is
  // now checked against the remaining buffer before it happens.
  static Tuple Deserialize(const std::string &data, const Schema &schema) {
    std::vector<Value> values;
    size_t offset = 0;
    for (const auto &col : schema.Columns()) {
      if (col.type == TypeId::INTEGER) {
        if (offset + sizeof(int64_t) > data.size()) throw std::runtime_error("corrupt tuple: truncated INTEGER field");
        int64_t x;
        memcpy(&x, data.data() + offset, sizeof(x));
        offset += sizeof(x);
        values.push_back(Value::Int(x));
      } else {
        if (offset + sizeof(int32_t) > data.size()) throw std::runtime_error("corrupt tuple: truncated VARCHAR length");
        int32_t len;
        memcpy(&len, data.data() + offset, sizeof(len));
        offset += sizeof(len);
        if (len < 0 || offset + static_cast<size_t>(len) > data.size())
          throw std::runtime_error("corrupt tuple: VARCHAR length out of bounds");
        values.push_back(Value::Str(data.substr(offset, len)));
        offset += len;
      }
    }
    return Tuple(std::move(values));
  }

 private:
  std::vector<Value> values_;
};

}  // namespace minidb
