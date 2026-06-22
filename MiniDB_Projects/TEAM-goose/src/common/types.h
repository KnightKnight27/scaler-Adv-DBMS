#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include <stdexcept>
#include <sstream>

namespace minidb {

// core value types

enum class ValueType : uint8_t { INT32 = 1, FLOAT64 = 2, STRING = 3, NONE = 0 };

struct Value {
    ValueType type = ValueType::NONE;
    int32_t    int_val = 0;
    double     float_val = 0.0;
    std::string str_val;

    Value() = default;
    Value(int32_t v)  : type(ValueType::INT32), int_val(v) {}
    Value(double v)   : type(ValueType::FLOAT64), float_val(v) {}
    Value(const std::string& v) : type(ValueType::STRING), str_val(v) {}
    Value(const char* v)        : type(ValueType::STRING), str_val(v) {}

    bool operator==(const Value& o) const;
    bool operator<(const Value& o) const;
    bool operator>(const Value& o) const  { return o < *this; }
    bool operator<=(const Value& o) const { return !(o < *this); }
    bool operator>=(const Value& o) const { return !(*this < o); }
    bool operator!=(const Value& o) const { return !(*this == o); }

    std::string to_string() const;
};

// a record (row) is an ordered list of values.
using Record = std::vector<Value>;

// key used for indexing — typically a single value.
using Key = Value;

// identifiers

using PageID    = uint32_t;   // page identifier
using TableID   = uint32_t;   // table identifier
using ColumnID  = uint32_t;   // column position (0-based)
using TxnID     = uint32_t;   // transaction identifier
using LSN       = uint64_t;   // log sequence number

constexpr PageID  INVALID_PAGE = 0;
constexpr TxnID   INVALID_TXN  = 0;
constexpr size_t  PAGE_SIZE    = 4096;    // 4 kb

// small helper: convert string to uppercase

inline std::string to_upper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return r;
}

// serialisation helpers (binary i/o for pages / wal)

inline void write_i32(std::ostream& os, int32_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
inline void write_u32(std::ostream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
inline void write_u64(std::ostream& os, uint64_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
inline void write_f64(std::ostream& os, double v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

inline int32_t read_i32(std::istream& is) {
    int32_t v; is.read(reinterpret_cast<char*>(&v), sizeof(v)); return v;
}
inline uint32_t read_u32(std::istream& is) {
    uint32_t v; is.read(reinterpret_cast<char*>(&v), sizeof(v)); return v;
}
inline uint64_t read_u64(std::istream& is) {
    uint64_t v; is.read(reinterpret_cast<char*>(&v), sizeof(v)); return v;
}
inline double read_f64(std::istream& is) {
    double v; is.read(reinterpret_cast<char*>(&v), sizeof(v)); return v;
}

inline void write_string(std::ostream& os, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    write_u32(os, len);
    os.write(s.data(), len);
}
inline std::string read_string(std::istream& is) {
    uint32_t len = read_u32(is);
    std::string s(len, '\0');
    is.read(&s[0], len);
    return s;
}

inline void write_value(std::ostream& os, const Value& v) {
    os.put(static_cast<char>(v.type));
    switch (v.type) {
        case ValueType::INT32:   write_i32(os, v.int_val); break;
        case ValueType::FLOAT64: write_f64(os, v.float_val); break;
        case ValueType::STRING:  write_string(os, v.str_val); break;
        case ValueType::NONE: break;
    }
}
inline Value read_value(std::istream& is) {
    auto type = static_cast<ValueType>(is.get());
    switch (type) {
        case ValueType::INT32:   return Value(read_i32(is));
        case ValueType::FLOAT64: return Value(read_f64(is));
        case ValueType::STRING:  return Value(read_string(is));
        default: return Value();
    }
}

inline void write_record(std::ostream& os, const Record& rec) {
    uint32_t cols = static_cast<uint32_t>(rec.size());
    write_u32(os, cols);
    for (const auto& v : rec) write_value(os, v);
}
inline Record read_record(std::istream& is) {
    uint32_t cols = read_u32(is);
    Record rec; rec.reserve(cols);
    for (uint32_t i = 0; i < cols; ++i) rec.push_back(read_value(is));
    return rec;
}

} // namespace minidb
