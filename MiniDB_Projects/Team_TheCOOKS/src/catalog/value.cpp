#include "catalog/value.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "common/serialize.h"

namespace walterdb {

const char* type_name(TypeId t) {
  switch (t) {
    case TypeId::Integer: return "INTEGER";
    case TypeId::Double: return "DOUBLE";
    case TypeId::Varchar: return "VARCHAR";
    case TypeId::Boolean: return "BOOLEAN";
  }
  return "UNKNOWN";
}

bool parse_type_name(const std::string& kw, TypeId* out) {
  std::string u;
  u.reserve(kw.size());
  for (char c : kw) u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  if (u == "INT" || u == "INTEGER" || u == "BIGINT") { *out = TypeId::Integer; return true; }
  if (u == "DOUBLE" || u == "FLOAT" || u == "REAL") { *out = TypeId::Double; return true; }
  if (u == "VARCHAR" || u == "TEXT" || u == "STRING" || u == "CHAR") { *out = TypeId::Varchar; return true; }
  if (u == "BOOL" || u == "BOOLEAN") { *out = TypeId::Boolean; return true; }
  return false;
}

double Value::numeric() const {
  return type_ == TypeId::Integer ? static_cast<double>(as_integer()) : as_double();
}

int Value::compare(const Value& other) const {
  // NULL handling: NULL sorts before any non-NULL; two NULLs are equal.
  bool ln = is_null(), rn = other.is_null();
  if (ln || rn) {
    if (ln && rn) return 0;
    return ln ? -1 : 1;
  }

  // Numeric cross-type comparison (Integer vs Double) goes through double.
  bool l_num = (type_ == TypeId::Integer || type_ == TypeId::Double);
  bool r_num = (other.type_ == TypeId::Integer || other.type_ == TypeId::Double);
  if (l_num && r_num) {
    if (type_ == TypeId::Integer && other.type_ == TypeId::Integer) {
      int64_t a = as_integer(), b = other.as_integer();
      return (a < b) ? -1 : (a > b ? 1 : 0);
    }
    double a = numeric(), b = other.numeric();
    return (a < b) ? -1 : (a > b ? 1 : 0);
  }

  // Non-numeric operands of differing types (or numeric vs non-numeric) are not
  // value-comparable; order them by TypeId so compare() stays a TOTAL order and
  // never reads the wrong variant alternative below (e.g. as_varchar() on an
  // int).  This makes `WHERE name < 5` evaluate cleanly instead of throwing.
  if (type_ != other.type_) return type_ < other.type_ ? -1 : 1;

  switch (type_) {
    case TypeId::Varchar: {
      int c = as_varchar().compare(other.as_varchar());
      return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    case TypeId::Boolean: {
      bool a = as_boolean(), b = other.as_boolean();
      return (a == b) ? 0 : (!a ? -1 : 1);
    }
    default:
      return 0;
  }
}

std::string Value::encode_key() const {
  // A NULL is encoded as a single 0x00 tag byte that sorts before the 0x01-led
  // encoding of any present value, preserving "NULL sorts first".
  if (is_null()) return std::string(1, '\0');
  std::string out(1, '\1');
  switch (type_) {
    case TypeId::Integer:
      out += encode_int64_key(as_integer());
      break;
    case TypeId::Double: {
      // Reuse the integer order-preserving trick on the bit pattern, with the
      // standard IEEE-754 sign flip so negatives order correctly.
      uint64_t bits;
      double d = as_double();
      std::memcpy(&bits, &d, sizeof(bits));
      bits = (bits & (uint64_t{1} << 63)) ? ~bits : (bits | (uint64_t{1} << 63));
      char buf[8];
      for (int i = 7; i >= 0; --i) { buf[i] = static_cast<char>(bits & 0xFF); bits >>= 8; }
      out.append(buf, 8);
      break;
    }
    case TypeId::Boolean:
      out.push_back(as_boolean() ? '\1' : '\0');
      break;
    case TypeId::Varchar:
      out += as_varchar();  // raw bytes are already lexicographically ordered
      break;
  }
  return out;
}

std::string Value::to_string() const {
  if (is_null()) return "NULL";
  switch (type_) {
    case TypeId::Integer: return std::to_string(as_integer());
    case TypeId::Double: {
      std::string s = std::to_string(as_double());
      return s;
    }
    case TypeId::Boolean: return as_boolean() ? "true" : "false";
    case TypeId::Varchar: return as_varchar();
  }
  return "?";
}

}  // namespace walterdb
