#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
#include "common/config.h"

namespace minidb {

// Supported SQL column types. INTEGER is stored as a 64-bit signed integer,
// VARCHAR as a length-prefixed byte string.
enum class TypeId { INVALID, INTEGER, VARCHAR };

inline const char *TypeName(TypeId t) {
  switch (t) {
    case TypeId::INTEGER: return "INTEGER";
    case TypeId::VARCHAR: return "VARCHAR";
    default:              return "INVALID";
  }
}

// A single SQL value. A tagged union of the supported types; kept as a plain
// struct (rather than std::variant) so the serialization format is explicit
// and easy to explain.
struct Value {
  TypeId      type{TypeId::INVALID};
  int64_t     i{0};
  std::string s;

  Value() = default;
  explicit Value(int64_t v) : type(TypeId::INTEGER), i(v) {}
  explicit Value(std::string v) : type(TypeId::VARCHAR), s(std::move(v)) {}

  static Value Int(int64_t v) { return Value(v); }
  static Value Str(std::string v) { return Value(std::move(v)); }

  bool IsNull() const { return type == TypeId::INVALID; }

  // Three-way compare against another value of the SAME type.
  // Returns <0, 0, >0. Comparing mismatched types throws.
  int Compare(const Value &o) const {
    if (type != o.type) throw std::runtime_error("Value::Compare type mismatch");
    if (type == TypeId::INTEGER) return (i < o.i) ? -1 : (i > o.i ? 1 : 0);
    return s.compare(o.s) < 0 ? -1 : (s.compare(o.s) > 0 ? 1 : 0);
  }

  bool operator==(const Value &o) const { return type == o.type && Compare(o) == 0; }
  bool operator<(const Value &o) const { return Compare(o) < 0; }

  std::string ToString() const {
    if (type == TypeId::INTEGER) return std::to_string(i);
    if (type == TypeId::VARCHAR) return s;
    return "NULL";
  }
};

// Record identifier: the physical address of a tuple = (page, slot).
// Stable for the lifetime of a row so indexes can point at it.
struct RID {
  page_id_t page_id{INVALID_PAGE_ID};
  slot_id_t slot{-1};

  bool operator==(const RID &o) const { return page_id == o.page_id && slot == o.slot; }
  bool IsValid() const { return page_id != INVALID_PAGE_ID; }
  std::string ToString() const {
    return "(" + std::to_string(page_id) + "," + std::to_string(slot) + ")";
  }
};

}  // namespace minidb
