#include "storage/tuple.h"
#include <cstring>
#include <stdexcept>

namespace minidb {

Bytes serialize(const Row& row, const Schema& schema) {
  if (row.size() != schema.size())
    throw std::runtime_error("serialize: row arity does not match schema");

  Bytes out;
  for (std::size_t i = 0; i < schema.columns.size(); ++i) {
    if (schema.columns[i].type == ColumnType::Int) {
      int64_t v = std::get<int64_t>(row[i]);
      out.append(reinterpret_cast<const char*>(&v), sizeof(v));
    } else {
      const std::string& s = std::get<std::string>(row[i]);
      uint16_t len = static_cast<uint16_t>(s.size());
      out.append(reinterpret_cast<const char*>(&len), sizeof(len));
      out.append(s);
    }
  }
  return out;
}

Row deserialize(const Bytes& bytes, const Schema& schema) {
  Row row;
  row.reserve(schema.size());
  std::size_t pos = 0;
  for (const Column& col : schema.columns) {
    if (col.type == ColumnType::Int) {
      int64_t v;
      std::memcpy(&v, bytes.data() + pos, sizeof(v));
      pos += sizeof(v);
      row.emplace_back(v);
    } else {
      uint16_t len;
      std::memcpy(&len, bytes.data() + pos, sizeof(len));
      pos += sizeof(len);
      row.emplace_back(std::string(bytes.data() + pos, len));
      pos += len;
    }
  }
  return row;
}

}  // namespace minidb
