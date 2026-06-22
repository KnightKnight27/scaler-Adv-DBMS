#include "record/tuple.h"

#include <cstring>

namespace minidb {
namespace tuple {

void Serialize(const Schema& schema, const std::vector<Value>& values,
               std::vector<char>& out) {
  out.assign(schema.RecordSize(), 0);
  char* p = out.data();
  for (size_t c = 0; c < schema.columns.size(); ++c) {
    const Column& col = schema.columns[c];
    const Value& v = values[c];
    if (col.type == ValueType::INT) {
      int32_t iv = v.i;
      // Little-endian, byte by byte (portable, no struct padding).
      p[0] = static_cast<char>(iv & 0xFF);
      p[1] = static_cast<char>((iv >> 8) & 0xFF);
      p[2] = static_cast<char>((iv >> 16) & 0xFF);
      p[3] = static_cast<char>((iv >> 24) & 0xFF);
    } else {
      int n = static_cast<int>(v.s.size());
      if (n > col.length) n = col.length;
      std::memcpy(p, v.s.data(), n);
      // remaining bytes already zero from out.assign
    }
    p += col.length;
  }
}

std::vector<Value> Deserialize(const Schema& schema, const char* data) {
  std::vector<Value> values;
  values.reserve(schema.columns.size());
  const char* p = data;
  for (const Column& col : schema.columns) {
    if (col.type == ValueType::INT) {
      uint32_t u = (static_cast<uint8_t>(p[0])) |
                   (static_cast<uint8_t>(p[1]) << 8) |
                   (static_cast<uint8_t>(p[2]) << 16) |
                   (static_cast<uint8_t>(p[3]) << 24);
      values.push_back(Value::Int(static_cast<int32_t>(u)));
    } else {
      // Trim at first NUL or at the column's fixed width.
      int len = 0;
      while (len < col.length && p[len] != '\0') ++len;
      values.push_back(Value::Varchar(std::string(p, p + len)));
    }
    p += col.length;
  }
  return values;
}

std::string ToString(const Schema& schema, const std::vector<Value>& values) {
  std::string out = "(";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out += ", ";
    out += values[i].ToString();
  }
  out += ")";
  return out;
}

}  // namespace tuple
}  // namespace minidb
