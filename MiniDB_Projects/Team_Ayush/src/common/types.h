#pragma once
#include <cstdint>
#include <string>
#include "common/config.h"

namespace minidb {

// The two value types MiniDB supports. Keeping the type system tiny (a 32-bit
// integer and a fixed-capacity string) is the single biggest simplification in
// the engine: every tuple becomes fixed-length, so heap pages are simple
// fixed-slot arrays and WAL before/after images are trivial byte ranges.
enum class ValueType { INT, VARCHAR };

// A tagged value. We avoid std::variant (not reliably available on the target
// C++14/MinGW toolchain) in favour of a plain struct.
struct Value {
  ValueType   type;
  int32_t     i;   // valid when type == INT
  std::string s;   // valid when type == VARCHAR

  Value() : type(ValueType::INT), i(0) {}

  static Value Int(int32_t v) {
    Value x; x.type = ValueType::INT; x.i = v; return x;
  }
  static Value Varchar(const std::string& v) {
    Value x; x.type = ValueType::VARCHAR; x.s = v; return x;
  }

  // Ordering helper used by predicates, indexes and joins. Comparing values of
  // different types is undefined for our purposes; callers compare like-typed
  // values only. Returns <0, 0, >0.
  int Compare(const Value& o) const {
    if (type == ValueType::INT) {
      return (i < o.i) ? -1 : (i > o.i) ? 1 : 0;
    }
    return s.compare(o.s);
  }

  std::string ToString() const {
    return type == ValueType::INT ? std::to_string(i) : s;
  }
};

// A record identifier: the physical address of a tuple = (page, slot).
struct RID {
  PageId  page;
  int16_t slot;

  RID() : page(INVALID_PAGE_ID), slot(-1) {}
  RID(PageId p, int16_t s) : page(p), slot(s) {}

  bool operator==(const RID& o) const { return page == o.page && slot == o.slot; }
};

}  // namespace minidb
