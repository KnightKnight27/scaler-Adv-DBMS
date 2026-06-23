#pragma once
// Core value/tuple types and (de)serialization for MiniDB.
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <ostream>

namespace minidb {

enum class TypeId : uint8_t { Int = 1, Double = 2, Varchar = 3 };

inline const char* type_name(TypeId t) {
  switch (t) {
    case TypeId::Int: return "INT";
    case TypeId::Double: return "DOUBLE";
    case TypeId::Varchar: return "VARCHAR";
  }
  return "?";
}

// A single column value. v1 has no NULLs.
class Value {
 public:
  Value() : kind_(TypeId::Int), int_val_(0) {}
  explicit Value(int64_t i) : kind_(TypeId::Int), int_val_(i) {}
  explicit Value(double d) : kind_(TypeId::Double), dbl_val_(d) {}
  explicit Value(const std::string& s) : kind_(TypeId::Varchar), str_val_(new std::string(s)) {}
  explicit Value(std::string&& s) : kind_(TypeId::Varchar), str_val_(new std::string(std::move(s))) {}

  Value(const Value& other) : kind_(other.kind_) {
    switch (kind_) {
      case TypeId::Int: int_val_ = other.int_val_; break;
      case TypeId::Double: dbl_val_ = other.dbl_val_; break;
      case TypeId::Varchar: str_val_ = new std::string(*other.str_val_); break;
    }
  }

  Value(Value&& other) noexcept : kind_(other.kind_) {
    switch (kind_) {
      case TypeId::Int: int_val_ = other.int_val_; break;
      case TypeId::Double: dbl_val_ = other.dbl_val_; break;
      case TypeId::Varchar: str_val_ = other.str_val_; other.str_val_ = nullptr; break;
    }
    other.kind_ = TypeId::Int;
    other.int_val_ = 0;
  }

  Value& operator=(const Value& other) {
    if (this != &other) {
      this->~Value();
      new (this) Value(other);
    }
    return *this;
  }

  Value& operator=(Value&& other) noexcept {
    if (this != &other) {
      this->~Value();
      new (this) Value(std::move(other));
    }
    return *this;
  }

  ~Value() {
    if (kind_ == TypeId::Varchar) delete str_val_;
  }

  TypeId type() const { return kind_; }

  int64_t as_int() const {
    if (kind_ != TypeId::Int) throw std::runtime_error("Value is not INT");
    return int_val_;
  }
  double as_double() const {
    if (kind_ != TypeId::Double) throw std::runtime_error("Value is not DOUBLE");
    return dbl_val_;
  }
  const std::string& as_string() const {
    if (kind_ != TypeId::Varchar) throw std::runtime_error("Value is not VARCHAR");
    return *str_val_;
  }

  // Total ordering within the same type; cross-type compares by type id.
  int compare(const Value& o) const {
    if (kind_ != o.kind_) return kind_ < o.kind_ ? -1 : 1;
    switch (kind_) {
      case TypeId::Int: {
        int64_t a = int_val_, b = o.int_val_;
        return a < b ? -1 : (a > b ? 1 : 0);
      }
      case TypeId::Double: {
        double a = dbl_val_, b = o.dbl_val_;
        return a < b ? -1 : (a > b ? 1 : 0);
      }
      case TypeId::Varchar: {
        const std::string& a = *str_val_;
        const std::string& b = *o.str_val_;
        return a < b ? -1 : (a > b ? 1 : 0);
      }
    }
    return 0;
  }
  bool operator==(const Value& o) const { return compare(o) == 0; }
  bool operator<(const Value& o) const { return compare(o) < 0; }

  std::string to_string() const {
    switch (kind_) {
      case TypeId::Int: return std::to_string(as_int());
      case TypeId::Double: return std::to_string(as_double());
      case TypeId::Varchar: return as_string();
    }
    return std::string();
  }

 private:
  TypeId kind_;
  union {
    int64_t int_val_;
    double dbl_val_;
    std::string* str_val_;
  };
};

inline std::ostream& operator<<(std::ostream& os, const Value& v) {
  return os << v.to_string();
}

// A row: an ordered list of column values.
using Tuple = std::vector<Value>;

}  // namespace minidb
