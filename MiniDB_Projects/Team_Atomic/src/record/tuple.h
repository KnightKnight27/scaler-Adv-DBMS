#pragma once
#include <cstring>
#include <string>
#include <vector>
#include "common/types.h"
#include "record/schema.h"

namespace minidb {

// A row of values. Serializes to a compact byte buffer for heap/LSM storage:
//   INTEGER -> 8 bytes (host endianness; single-machine engine)
//   VARCHAR -> 4-byte length prefix + raw bytes
class Tuple {
 public:
  Tuple() = default;
  explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}

  const std::vector<Value>& Values() const { return values_; }
  const Value& GetValue(size_t i) const { return values_[i]; }
  size_t Size() const { return values_.size(); }

  // Serialize according to `schema` (column order/types).
  std::string Serialize(const Schema& schema) const {
    std::string out;
    for (size_t i = 0; i < schema.ColumnCount(); i++) {
      const Value& v = values_[i];
      if (schema.GetColumn(i).type == TypeId::INTEGER) {
        int64_t x = v.i;
        out.append(reinterpret_cast<const char*>(&x), sizeof(x));
      } else {
        uint32_t len = static_cast<uint32_t>(v.s.size());
        out.append(reinterpret_cast<const char*>(&len), sizeof(len));
        out.append(v.s);
      }
    }
    return out;
  }

  // Inverse of Serialize.
  static Tuple Deserialize(const char* data, const Schema& schema) {
    std::vector<Value> vals;
    size_t off = 0;
    for (size_t i = 0; i < schema.ColumnCount(); i++) {
      if (schema.GetColumn(i).type == TypeId::INTEGER) {
        int64_t x;
        std::memcpy(&x, data + off, sizeof(x));
        off += sizeof(x);
        vals.push_back(Value::Int(x));
      } else {
        uint32_t len;
        std::memcpy(&len, data + off, sizeof(len));
        off += sizeof(len);
        vals.push_back(Value::Str(std::string(data + off, len)));
        off += len;
      }
    }
    return Tuple(std::move(vals));
  }

 private:
  std::vector<Value> values_;
};

}  // namespace minidb
