#include "catalog/tuple.h"

#include <cctype>

#include "common/serialize.h"

namespace axiomdb {

std::optional<size_t> Schema::index_of(const std::string& name) const {
  auto ieq = [](const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i])))
        return false;
    }
    return true;
  };
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (ieq(columns_[i].name, name)) return i;
  }
  return std::nullopt;
}

std::optional<size_t> Schema::primary_key_index() const {
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].primary_key) return i;
  }
  return std::nullopt;
}

std::string Tuple::encode(const Schema& schema) const {
  const size_t n = schema.num_columns();
  ByteWriter w;

  // Null bitmap: bit i set => column i is NULL.
  const size_t bitmap_bytes = (n + 7) / 8;
  std::string bitmap(bitmap_bytes, '\0');
  for (size_t i = 0; i < n; ++i) {
    if (i < values_.size() && values_[i].is_null()) {
      bitmap[i / 8] |= static_cast<char>(1u << (i % 8));
    }
  }
  w.put_bytes(bitmap);

  for (size_t i = 0; i < n; ++i) {
    const Value& v = values_[i];
    if (v.is_null()) continue;
    switch (schema.column(i).type) {
      case TypeId::Integer: w.put_i64(v.as_integer()); break;
      case TypeId::Double: w.put_double(v.as_double()); break;
      case TypeId::Boolean: w.put_u8(v.as_boolean() ? 1 : 0); break;
      case TypeId::Varchar: w.put_string(v.as_varchar()); break;
    }
  }
  return w.take();
}

Tuple Tuple::decode(const Schema& schema, std::string_view bytes) {
  const size_t n = schema.num_columns();
  ByteReader r(bytes);

  const size_t bitmap_bytes = (n + 7) / 8;
  std::string_view bitmap = r.get_bytes(bitmap_bytes);

  std::vector<Value> values;
  values.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    bool is_null = (static_cast<unsigned char>(bitmap[i / 8]) >> (i % 8)) & 1u;
    TypeId t = schema.column(i).type;
    if (is_null) {
      values.push_back(Value::make_null(t));
      continue;
    }
    switch (t) {
      case TypeId::Integer: values.push_back(Value::make_integer(r.get_i64())); break;
      case TypeId::Double: values.push_back(Value::make_double(r.get_double())); break;
      case TypeId::Boolean: values.push_back(Value::make_boolean(r.get_u8() != 0)); break;
      case TypeId::Varchar: values.push_back(Value::make_varchar(std::string(r.get_string()))); break;
    }
  }
  return Tuple(std::move(values));
}

std::string Tuple::to_string() const {
  std::string out = "(";
  for (size_t i = 0; i < values_.size(); ++i) {
    if (i) out += ", ";
    out += values_[i].to_string();
  }
  out += ")";
  return out;
}

}  // namespace axiomdb
