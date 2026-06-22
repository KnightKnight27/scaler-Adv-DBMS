#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
#include "common/config.h"

namespace minidb {

// A MiniDB error surfaced to the shell / caller.
struct DBError : std::runtime_error {
  explicit DBError(const std::string& msg) : std::runtime_error(msg) {}
};

// Which storage engine backs a table.
enum class EngineType { Heap, LSM };

// Column / value types supported by the engine.
enum class TypeId { INTEGER, VARCHAR };

inline std::string TypeName(TypeId t) {
  switch (t) {
    case TypeId::INTEGER: return "INTEGER";
    case TypeId::VARCHAR: return "VARCHAR";
  }
  return "?";
}

// A dynamically-typed value. INTEGER stores in `i`, VARCHAR in `s`.
struct Value {
  TypeId type = TypeId::INTEGER;
  int64_t i = 0;
  std::string s;

  Value() = default;
  static Value Int(int64_t v) { Value x; x.type = TypeId::INTEGER; x.i = v; return x; }
  static Value Str(std::string v) { Value x; x.type = TypeId::VARCHAR; x.s = std::move(v); return x; }

  std::string ToString() const {
    return type == TypeId::INTEGER ? std::to_string(i) : s;
  }

  // Three-way comparison (-1 / 0 / 1). Only meaningful for same-typed values.
  int Compare(const Value& o) const {
    if (type == TypeId::INTEGER) {
      return (i < o.i) ? -1 : (i > o.i ? 1 : 0);
    }
    return (s < o.s) ? -1 : (s > o.s ? 1 : 0);
  }

  bool operator==(const Value& o) const { return type == o.type && Compare(o) == 0; }
  bool operator<(const Value& o) const { return Compare(o) < 0; }
};

// Record identifier: which heap page, which slot on that page.
struct RID {
  page_id_t page_id = INVALID_PAGE_ID;
  slot_id_t slot_id = -1;

  bool operator==(const RID& o) const {
    return page_id == o.page_id && slot_id == o.slot_id;
  }
  std::string ToString() const {
    return "(" + std::to_string(page_id) + "," + std::to_string(slot_id) + ")";
  }
};

}  // namespace minidb
