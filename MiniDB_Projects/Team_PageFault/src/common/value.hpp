#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>

namespace minidb {

// The only two column types MiniDB supports.
enum class Type { INT, TEXT };

inline const char* typeName(Type t) { return t == Type::INT ? "INT" : "TEXT"; }

// A tagged union of the two supported value kinds. Kept as a plain struct (not
// std::variant) so the (de)serialization code stays obvious.
struct Value {
    Type type = Type::INT;
    int64_t i = 0;
    std::string s;

    Value() = default;
    static Value makeInt(int64_t v) { Value x; x.type = Type::INT; x.i = v; return x; }
    static Value makeText(std::string v) { Value x; x.type = Type::TEXT; x.s = std::move(v); return x; }

    std::string toString() const {
        return type == Type::INT ? std::to_string(i) : s;
    }

    // Three-way comparison: <0, 0, >0. Comparing across types is a logic error.
    int compare(const Value& o) const {
        if (type != o.type) throw std::runtime_error("type mismatch in comparison");
        if (type == Type::INT) return (i < o.i) ? -1 : (i > o.i ? 1 : 0);
        return s.compare(o.s) < 0 ? -1 : (s.compare(o.s) > 0 ? 1 : 0);
    }
};

} // namespace minidb
