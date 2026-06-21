#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace walterdb {

// ---------------------------------------------------------------------------
// The relational type system.
//
// WALterDB supports a deliberately small set of column types -- enough to write
// interesting queries (numbers, text, booleans) without turning the type system
// into a project of its own.  Integers are 64-bit throughout (one integer type,
// fewer edge cases).
// ---------------------------------------------------------------------------

enum class TypeId {
  Integer,  // 64-bit signed
  Double,   // IEEE-754 double
  Varchar,  // variable-length UTF-8/bytes
  Boolean,  // true / false
};

const char* type_name(TypeId t);

// Parse a SQL type keyword ("INT", "INTEGER", "BIGINT", "DOUBLE", "FLOAT",
// "VARCHAR", "TEXT", "BOOL", "BOOLEAN") into a TypeId.  Returns false on an
// unknown keyword.
bool parse_type_name(const std::string& kw, TypeId* out);

// ---------------------------------------------------------------------------
// Value -- a single typed cell.  A Value always carries its TypeId (even when
// NULL), so the engine can compare / encode it without a separate schema lookup
// in most paths.  NULL is represented by the monostate alternative.
// ---------------------------------------------------------------------------

class Value {
 public:
  Value() : type_(TypeId::Integer), data_(std::monostate{}) {}  // NULL integer

  static Value make_integer(int64_t v) { return Value(TypeId::Integer, v); }
  static Value make_double(double v) { return Value(TypeId::Double, v); }
  static Value make_varchar(std::string v) { return Value(TypeId::Varchar, std::move(v)); }
  static Value make_boolean(bool v) { return Value(TypeId::Boolean, v); }
  static Value make_null(TypeId t) { return Value(t, std::monostate{}); }

  TypeId type() const { return type_; }
  bool is_null() const { return std::holds_alternative<std::monostate>(data_); }

  int64_t as_integer() const { return std::get<int64_t>(data_); }
  double as_double() const { return std::get<double>(data_); }
  const std::string& as_varchar() const { return std::get<std::string>(data_); }
  bool as_boolean() const { return std::get<bool>(data_); }

  // Numeric value as double regardless of Integer/Double (for mixed arithmetic
  // / comparison). Undefined for non-numeric types.
  double numeric() const;

  // Total order used for sorting and predicate evaluation.  Returns <0, 0, >0.
  // Convention: NULL sorts before any non-NULL; two NULLs are equal.  Integers
  // and doubles compare numerically against each other.
  int compare(const Value& other) const;

  bool operator==(const Value& o) const { return compare(o) == 0; }
  bool operator<(const Value& o) const { return compare(o) < 0; }

  // Order-preserving byte encoding (see common/serialize.h) so a Value can be
  // used directly as a B+tree / KV key with correct range-scan ordering.
  std::string encode_key() const;

  // Human-readable form for query output and EXPLAIN.
  std::string to_string() const;

 private:
  using Data = std::variant<std::monostate, int64_t, double, std::string, bool>;
  Value(TypeId t, Data d) : type_(t), data_(std::move(d)) {}

  TypeId type_;
  Data data_;
};

}  // namespace walterdb
