#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <stdexcept>
#include <functional>

// Supported SQL column types. Kept minimal on purpose — the focus is on
// demonstrating the engine internals, not a complete type system.
enum class Type : uint8_t { INT = 0, VARCHAR = 1 };

// A single column value. is_null=true means SQL NULL regardless of type.
struct Value {
    Type type    = Type::INT;
    bool is_null = false;
    std::variant<int64_t, std::string> data;

    static Value make_int(int64_t v)        { return {Type::INT,     false, v}; }
    static Value make_varchar(std::string v){ return {Type::VARCHAR,  false, std::move(v)}; }
    static Value make_null(Type t)          { return {t,             true,  {}}; }

    int64_t as_int() const {
        if (type != Type::INT) throw std::runtime_error("value is not INT");
        return std::get<int64_t>(data);
    }
    const std::string& as_str() const {
        if (type != Type::VARCHAR) throw std::runtime_error("value is not VARCHAR");
        return std::get<std::string>(data);
    }

    bool operator==(const Value& o) const {
        if (type != o.type || is_null != o.is_null) return false;
        if (is_null) return true;
        return data == o.data;
    }
    bool operator<(const Value& o) const {
        if (type == Type::INT) return as_int() < o.as_int();
        return as_str() < o.as_str();
    }
    bool operator<=(const Value& o) const { return !(o < *this); }
    bool operator>(const Value& o) const  { return o < *this; }
    bool operator>=(const Value& o) const { return !(*this < o); }
    bool operator!=(const Value& o) const { return !(*this == o); }

    std::string to_string() const {
        if (is_null) return "NULL";
        if (type == Type::INT) return std::to_string(as_int());
        return as_str();
    }
};

// Record ID: identifies one stored record by its page and slot.
// Used by the B+ Tree to point back to heap storage.
struct RID {
    uint32_t page_id = UINT32_MAX;
    uint16_t slot_id = UINT16_MAX;

    bool valid() const { return page_id != UINT32_MAX; }
    bool operator==(const RID& o) const { return page_id == o.page_id && slot_id == o.slot_id; }
    bool operator<(const RID& o) const {
        if (page_id != o.page_id) return page_id < o.page_id;
        return slot_id < o.slot_id;
    }
};

// Hash support so RID can be used as a key in unordered_map (needed by the
// MVCC version chain and lock manager).
namespace std {
template<> struct hash<RID> {
    size_t operator()(const RID& r) const noexcept {
        return hash<uint64_t>{}((uint64_t)r.page_id << 16 | r.slot_id);
    }
};
}
