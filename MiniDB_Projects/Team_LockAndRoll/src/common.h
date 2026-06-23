#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace minidb {

constexpr int PAGE_SIZE = 4096;
constexpr int INVALID_PAGE_ID = -1;

using page_id_t = int32_t;
using slot_id_t = int32_t;
using txn_id_t = int64_t;
using lsn_t = int64_t;
using timestamp_t = int64_t;

constexpr lsn_t INVALID_LSN = -1;
constexpr txn_id_t INVALID_TXN = -1;

struct DBException : std::runtime_error {
  explicit DBException(const std::string& msg) : std::runtime_error(msg) {}
};

struct AbortException : DBException {
  explicit AbortException(const std::string& msg) : DBException(msg) {}
};

struct RID {
  page_id_t page_id = INVALID_PAGE_ID;
  slot_id_t slot = -1;

  bool operator==(const RID& o) const { return page_id == o.page_id && slot == o.slot; }
  bool operator!=(const RID& o) const { return !(*this == o); }
  bool operator<(const RID& o) const {
    return page_id != o.page_id ? page_id < o.page_id : slot < o.slot;
  }
  bool valid() const { return page_id != INVALID_PAGE_ID && slot >= 0; }
};

enum class TypeId { INTEGER, VARCHAR, BOOLEAN };

inline const char* type_name(TypeId t) {
  switch (t) {
    case TypeId::INTEGER: return "INTEGER";
    case TypeId::VARCHAR: return "VARCHAR";
    case TypeId::BOOLEAN: return "BOOLEAN";
  }
  return "?";
}

class Value {
 public:
  Value() : data_(std::monostate{}) {}
  explicit Value(int64_t v) : data_(v) {}
  explicit Value(std::string v) : data_(std::move(v)) {}
  explicit Value(bool v) : data_(v) {}

  static Value null() { return Value(); }

  bool is_null() const { return std::holds_alternative<std::monostate>(data_); }
  bool is_int() const { return std::holds_alternative<int64_t>(data_); }
  bool is_string() const { return std::holds_alternative<std::string>(data_); }
  bool is_bool() const { return std::holds_alternative<bool>(data_); }

  int64_t as_int() const { return std::get<int64_t>(data_); }
  const std::string& as_string() const { return std::get<std::string>(data_); }
  bool as_bool() const { return std::get<bool>(data_); }

  TypeId type() const {
    if (is_int()) return TypeId::INTEGER;
    if (is_string()) return TypeId::VARCHAR;
    return TypeId::BOOLEAN;
  }

  std::string to_string() const {
    if (is_null()) return "NULL";
    if (is_int()) return std::to_string(as_int());
    if (is_bool()) return as_bool() ? "true" : "false";
    return as_string();
  }

  // nulls sort first
  int compare(const Value& o) const {
    if (is_null() || o.is_null()) {
      if (is_null() && o.is_null()) return 0;
      return is_null() ? -1 : 1;
    }
    if (is_int() && o.is_int()) {
      int64_t a = as_int(), b = o.as_int();
      return a < b ? -1 : (a > b ? 1 : 0);
    }
    if (is_bool() && o.is_bool()) {
      return (as_bool() ? 1 : 0) - (o.as_bool() ? 1 : 0);
    }
    if (is_string() && o.is_string()) {
      return as_string().compare(o.as_string());
    }
    throw DBException("type mismatch in value comparison");
  }

  bool operator==(const Value& o) const { return compare(o) == 0; }

 private:
  std::variant<std::monostate, int64_t, std::string, bool> data_;
};

struct Column {
  std::string name;
  TypeId type;
};

class Schema {
 public:
  Schema() = default;
  explicit Schema(std::vector<Column> cols) : columns_(std::move(cols)) {}

  const std::vector<Column>& columns() const { return columns_; }
  size_t size() const { return columns_.size(); }
  const Column& column(size_t i) const { return columns_[i]; }

  // matches "table.col" or bare "col"
  int index_of(const std::string& name) const {
    for (size_t i = 0; i < columns_.size(); i++) {
      const std::string& c = columns_[i].name;
      if (c == name) return static_cast<int>(i);
      auto dot = c.find('.');
      if (dot != std::string::npos && c.substr(dot + 1) == name) return static_cast<int>(i);
    }
    return -1;
  }

 private:
  std::vector<Column> columns_;
};

// on-disk layout:
//   [uint16 nvalues][null bitmap, ceil(n/8) bytes][each non-null value]
//   INTEGER -> int64 (8B), BOOLEAN -> uint8 (1B), VARCHAR -> uint16 len + bytes
class Tuple {
 public:
  Tuple() = default;
  explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}

  const std::vector<Value>& values() const { return values_; }
  const Value& value(size_t i) const { return values_[i]; }
  size_t size() const { return values_.size(); }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> buf;
    auto put16 = [&](uint16_t v) { buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF); };
    auto put64 = [&](int64_t v) {
      for (int i = 0; i < 8; i++) buf.push_back((v >> (i * 8)) & 0xFF);
    };
    uint16_t n = static_cast<uint16_t>(values_.size());
    put16(n);
    size_t bitmap_bytes = (n + 7) / 8;
    size_t bitmap_off = buf.size();
    buf.resize(buf.size() + bitmap_bytes, 0);
    for (uint16_t i = 0; i < n; i++) {
      const Value& v = values_[i];
      if (v.is_null()) {
        buf[bitmap_off + i / 8] |= (1 << (i % 8));
        continue;
      }
      if (v.is_int()) {
        put64(v.as_int());
      } else if (v.is_bool()) {
        buf.push_back(v.as_bool() ? 1 : 0);
      } else {
        const std::string& s = v.as_string();
        put16(static_cast<uint16_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
      }
    }
    return buf;
  }

  // avail is the slot length; every read is bounds-checked against it so a torn tuple throws instead of reading past the page frame
  static Tuple deserialize(const uint8_t* data, size_t avail, const Schema& schema) {
    size_t off = 0;
    auto need = [&](size_t k) {
      if (off + k > avail) throw DBException("corrupt tuple: read out of bounds");
    };
    auto get16 = [&]() -> uint16_t {
      need(2);
      uint16_t v = data[off] | (data[off + 1] << 8);
      off += 2;
      return v;
    };
    auto get64 = [&]() -> int64_t {
      need(8);
      int64_t v = 0;
      for (int i = 0; i < 8; i++) v |= (static_cast<int64_t>(data[off + i]) << (i * 8));
      off += 8;
      return v;
    };
    uint16_t n = get16();
    if (n != schema.size()) throw DBException("corrupt tuple: value count mismatch");
    size_t bitmap_off = off;
    size_t bitmap_bytes = (n + 7) / 8;
    need(bitmap_bytes);
    off += bitmap_bytes;
    std::vector<Value> vals;
    vals.reserve(n);
    for (uint16_t i = 0; i < n; i++) {
      bool is_null = data[bitmap_off + i / 8] & (1 << (i % 8));
      if (is_null) {
        vals.push_back(Value::null());
        continue;
      }
      switch (schema.column(i).type) {
        case TypeId::INTEGER: vals.push_back(Value(get64())); break;
        case TypeId::BOOLEAN:
          need(1);
          vals.push_back(Value(static_cast<bool>(data[off++] != 0)));
          break;
        case TypeId::VARCHAR: {
          uint16_t len = get16();
          need(len);
          std::string s(reinterpret_cast<const char*>(data + off), len);
          off += len;
          vals.push_back(Value(std::move(s)));
          break;
        }
      }
    }
    return Tuple(std::move(vals));
  }

 private:
  std::vector<Value> values_;
};

}  // namespace minidb
