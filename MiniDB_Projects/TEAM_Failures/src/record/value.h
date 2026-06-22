// ============================================================================
// value.h  --  A single typed data cell, e.g. the integer 42 or the string
// "alice".  Values are what flows through the query engine: a tuple is a vector
// of Values, a WHERE comparison compares two Values, etc.
//
// MiniDB supports exactly two column types to keep the type system tiny and
// fully explainable:
//   * INTEGER -> 32-bit signed integer
//   * VARCHAR -> variable-length UTF-8 string
// Adding more types would mean extending this one file plus the serializer.
// ============================================================================
#pragma once

#include "common/common.h"

namespace minidb {

enum class TypeId { INTEGER, VARCHAR };

inline string TypeToString(TypeId t) {
  return t == TypeId::INTEGER ? "INTEGER" : "VARCHAR";
}

// A Value is "a type tag + the data".  We use variant as the storage, with
// a separate TypeId so that even a NULL-like empty value still knows its type.
class Value {
 public:
  Value() : type_(TypeId::INTEGER), data_(int32_t{0}) {}
  explicit Value(int32_t v) : type_(TypeId::INTEGER), data_(v) {}
  explicit Value(string v) : type_(TypeId::VARCHAR), data_(move(v)) {}

  TypeId type() const { return type_; }

  int32_t asInt() const {
    if (type_ != TypeId::INTEGER) throw ExecError("Value is not an INTEGER");
    return get<int32_t>(data_);
  }
  const string &asString() const {
    if (type_ != TypeId::VARCHAR) throw ExecError("Value is not a VARCHAR");
    return get<string>(data_);
  }

  // Three-way compare: <0, 0, >0.  Used by WHERE evaluation and the B+ Tree.
  // Comparing two different types is a programming error (the binder should have
  // caught it), so we throw rather than silently coerce.
  int compare(const Value &o) const {
    if (type_ != o.type_) throw ExecError("cannot compare values of different types");
    if (type_ == TypeId::INTEGER) {
      int32_t a = asInt(), b = o.asInt();
      return (a < b) ? -1 : (a > b ? 1 : 0);
    }
    return asString().compare(o.asString());
  }

  bool operator==(const Value &o) const { return compare(o) == 0; }
  bool operator<(const Value &o) const  { return compare(o) < 0; }

  string toString() const {
    return type_ == TypeId::INTEGER ? to_string(asInt()) : asString();
  }

 private:
  TypeId type_;
  variant<int32_t, string> data_;
};

}  // namespace minidb
