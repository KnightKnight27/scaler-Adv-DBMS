#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace minidb {

// MiniDB supports two column types — enough to model realistic relational
// schemas (an integer key + textual/attribute columns) without drowning the
// engine in a full type system.
enum class TypeId : uint8_t { INVALID = 0, INTEGER = 1, VARCHAR = 2 };

inline const char *TypeName(TypeId t) {
  switch (t) {
    case TypeId::INTEGER: return "INT";
    case TypeId::VARCHAR: return "VARCHAR";
    default: return "INVALID";
  }
}

// A tagged value. Comparisons assume both sides share a type (the binder
// guarantees this). Serialization is the Tuple's job, since only the schema
// knows the on-disk layout.
class Value {
 public:
  Value() : type_(TypeId::INVALID) {}
  explicit Value(int32_t v) : type_(TypeId::INTEGER), int_val_(v) {}
  explicit Value(std::string v) : type_(TypeId::VARCHAR), str_val_(std::move(v)) {}

  TypeId GetType() const { return type_; }
  bool IsNull() const { return type_ == TypeId::INVALID; }
  int32_t GetInt() const { return int_val_; }
  const std::string &GetString() const { return str_val_; }

  // Three-way compare: <0, 0, >0. INTEGER by value, VARCHAR lexicographically.
  int Compare(const Value &o) const {
    if (type_ == TypeId::INTEGER) {
      return (int_val_ < o.int_val_) ? -1 : (int_val_ > o.int_val_ ? 1 : 0);
    }
    return str_val_.compare(o.str_val_);
  }
  bool Equals(const Value &o) const { return type_ == o.type_ && Compare(o) == 0; }

  std::string ToString() const {
    return type_ == TypeId::INTEGER ? std::to_string(int_val_) : str_val_;
  }

 private:
  TypeId type_;
  int32_t int_val_{0};
  std::string str_val_;
};

}  // namespace minidb
