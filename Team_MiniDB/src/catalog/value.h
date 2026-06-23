#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>

namespace minidb {

enum class TypeId { INTEGER, VARCHAR };

// A single typed cell. Kept as a tagged union over the two types MiniDB
// supports - enough to express PKs, predicates, and joins without dragging
// in a full type system.
class Value {
 public:
  Value() : type_(TypeId::INTEGER), int_val_(0) {}
  static Value Int(int64_t v) { return Value(v); }
  static Value Str(std::string v) { return Value(std::move(v)); }

  TypeId GetType() const { return type_; }
  int64_t AsInt() const {
    if (type_ != TypeId::INTEGER) throw std::runtime_error("Value is not INTEGER");
    return int_val_;
  }
  const std::string &AsStr() const {
    if (type_ != TypeId::VARCHAR) throw std::runtime_error("Value is not VARCHAR");
    return str_val_;
  }

  bool operator==(const Value &o) const {
    if (type_ != o.type_) return false;
    return type_ == TypeId::INTEGER ? int_val_ == o.int_val_ : str_val_ == o.str_val_;
  }
  bool operator<(const Value &o) const {
    return type_ == TypeId::INTEGER ? int_val_ < o.int_val_ : str_val_ < o.str_val_;
  }

  std::string ToString() const {
    return type_ == TypeId::INTEGER ? std::to_string(int_val_) : str_val_;
  }

 private:
  explicit Value(int64_t v) : type_(TypeId::INTEGER), int_val_(v) {}
  explicit Value(std::string v) : type_(TypeId::VARCHAR), str_val_(std::move(v)) {}

  TypeId type_;
  int64_t int_val_ = 0;
  std::string str_val_;
};

}  // namespace minidb
