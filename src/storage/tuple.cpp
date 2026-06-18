#include "storage/tuple.h"

#include <cstring>

namespace minidb {

Tuple::Tuple(const std::vector<Value> &values, const Schema &schema) {
  // First pass: total size.
  uint32_t size = 0;
  for (size_t i = 0; i < schema.GetColumnCount(); i++) {
    if (schema.GetColumn(i).type == TypeId::INTEGER) {
      size += 4;
    } else {
      size += 4 + static_cast<uint32_t>(values[i].GetString().size());
    }
  }
  data_.resize(size);

  // Second pass: encode column by column.
  uint32_t off = 0;
  for (size_t i = 0; i < schema.GetColumnCount(); i++) {
    if (schema.GetColumn(i).type == TypeId::INTEGER) {
      int32_t v = values[i].GetInt();
      std::memcpy(&data_[off], &v, 4);
      off += 4;
    } else {
      const std::string &s = values[i].GetString();
      auto len = static_cast<uint32_t>(s.size());
      std::memcpy(&data_[off], &len, 4);
      off += 4;
      if (len > 0) std::memcpy(&data_[off], s.data(), len);
      off += len;
    }
  }
}

Value Tuple::GetValue(const Schema &schema, size_t col_idx) const {
  uint32_t off = 0;
  for (size_t i = 0; i <= col_idx && i < schema.GetColumnCount(); i++) {
    if (schema.GetColumn(i).type == TypeId::INTEGER) {
      if (i == col_idx) {
        int32_t v;
        std::memcpy(&v, &data_[off], 4);
        return Value(v);
      }
      off += 4;
    } else {
      uint32_t len;
      std::memcpy(&len, &data_[off], 4);
      if (i == col_idx) {
        return Value(std::string(&data_[off + 4], len));
      }
      off += 4 + len;
    }
  }
  return Value();  // out of range -> NULL
}

std::vector<Value> Tuple::GetValues(const Schema &schema) const {
  std::vector<Value> out;
  out.reserve(schema.GetColumnCount());
  for (size_t i = 0; i < schema.GetColumnCount(); i++) out.push_back(GetValue(schema, i));
  return out;
}

}  // namespace minidb
