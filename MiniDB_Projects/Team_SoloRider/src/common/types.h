#pragma once
// common/types.h — Shared type definitions used across all MiniDB modules.
//
// This header defines the fundamental types (page IDs, record IDs, column types,
// values, tuples, schemas) so that every module speaks the same language.
// Keeping these in one place avoids circular dependencies and makes it easy
// to see the "vocabulary" of the system at a glance.

#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <iostream>

namespace minidb {

// ─── Page & Record Identifiers ───────────────────────────────
// A page_id uniquely identifies a page on disk (or in the buffer pool).
// INVALID_PAGE_ID is used as a sentinel (e.g., "no next page").
using page_id_t = uint32_t;
static constexpr page_id_t INVALID_PAGE_ID = 0xFFFFFFFF;

// A slot_id identifies a tuple's position within a page's slot directory.
using slot_id_t = uint16_t;
static constexpr slot_id_t INVALID_SLOT_ID = 0xFFFF;

// A frame_id identifies a slot in the buffer pool's frame array.
using frame_id_t = uint32_t;

// A transaction_id uniquely identifies a transaction.
using txn_id_t = uint32_t;
static constexpr txn_id_t INVALID_TXN_ID = 0;

// A log sequence number identifies a position in the write-ahead log.
using lsn_t = uint64_t;
static constexpr lsn_t INVALID_LSN = 0;

// ─── Record ID ───────────────────────────────────────────────
// Combines page_id + slot_id to uniquely locate a tuple in a heap file.
struct RecordId {
    page_id_t page_id = INVALID_PAGE_ID;
    slot_id_t slot_id = INVALID_SLOT_ID;

    bool operator==(const RecordId& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }
    bool operator!=(const RecordId& other) const {
        return !(*this == other);
    }
    bool operator<(const RecordId& other) const {
        if (page_id != other.page_id) return page_id < other.page_id;
        return slot_id < other.slot_id;
    }
    bool is_valid() const {
        return page_id != INVALID_PAGE_ID && slot_id != INVALID_SLOT_ID;
    }
};

} // namespace minidb

namespace std {
    template <>
    struct hash<minidb::RecordId> {
        size_t operator()(const minidb::RecordId& rid) const {
            return hash<uint32_t>()(rid.page_id) ^ (hash<uint16_t>()(rid.slot_id) << 1);
        }
    };
} // namespace std

namespace minidb {

// The supported SQL data types. Kept minimal for an educational DB.
enum class ColumnType : uint8_t {
    INT     = 0,   // 4-byte signed integer
    FLOAT   = 1,   // 8-byte double
    VARCHAR = 2,   // variable-length string (up to max_length)
    BOOL    = 3    // 1-byte boolean
};

inline std::string column_type_to_string(ColumnType t) {
    switch (t) {
        case ColumnType::INT:     return "INT";
        case ColumnType::FLOAT:   return "FLOAT";
        case ColumnType::VARCHAR: return "VARCHAR";
        case ColumnType::BOOL:    return "BOOL";
        default:                  return "UNKNOWN";
    }
}

// ─── Column Definition ──────────────────────────────────────
// Describes a single column in a table's schema.
struct Column {
    std::string name;
    ColumnType type;
    uint16_t max_length;  // Only meaningful for VARCHAR; 0 for fixed types.

    Column() : type(ColumnType::INT), max_length(0) {}
    Column(std::string n, ColumnType t, uint16_t ml = 0)
        : name(std::move(n)), type(t), max_length(ml) {}
};

// ─── Schema ──────────────────────────────────────────────────
// A table schema: an ordered list of columns.
struct Schema {
    std::vector<Column> columns;

    Schema() = default;
    explicit Schema(std::vector<Column> cols) : columns(std::move(cols)) {}

    // Find column index by name, returns -1 if not found.
    int find_column(const std::string& name) const {
        for (size_t i = 0; i < columns.size(); i++) {
            if (columns[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }

    size_t column_count() const { return columns.size(); }
};

// ─── Value ───────────────────────────────────────────────────
// A dynamically typed value. Uses std::variant so we can store any
// supported column type in a single container.
// std::monostate represents NULL.
using Value = std::variant<std::monostate, int, double, std::string, bool>;

// Helper to check if a Value is NULL
inline bool is_null(const Value& v) {
    return std::holds_alternative<std::monostate>(v);
}

// Convert a Value to a human-readable string for display.
inline std::string value_to_string(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return "NULL";
    if (std::holds_alternative<int>(v)) return std::to_string(std::get<int>(v));
    if (std::holds_alternative<double>(v)) {
        std::ostringstream oss;
        oss << std::get<double>(v);
        return oss.str();
    }
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
    return "UNKNOWN";
}

// Compare two Values. Returns <0 if a<b, 0 if equal, >0 if a>b.
// Both values must be the same type (or both NULL).
inline int compare_values(const Value& a, const Value& b) {
    if (is_null(a) && is_null(b)) return 0;
    if (is_null(a)) return -1;
    if (is_null(b)) return 1;

    if (std::holds_alternative<int>(a) && std::holds_alternative<int>(b)) {
        int va = std::get<int>(a), vb = std::get<int>(b);
        return (va < vb) ? -1 : (va > vb) ? 1 : 0;
    }
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        double va = std::get<double>(a), vb = std::get<double>(b);
        return (va < vb) ? -1 : (va > vb) ? 1 : 0;
    }
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a).compare(std::get<std::string>(b));
    }
    if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b)) {
        bool va = std::get<bool>(a), vb = std::get<bool>(b);
        return (va == vb) ? 0 : (va ? 1 : -1);
    }
    // Type mismatch — treat as incomparable
    throw std::runtime_error("Cannot compare values of different types");
}

// ─── Tuple ───────────────────────────────────────────────────
// A row of values. Carries its own RecordId so operators know where
// the tuple came from (useful for updates/deletes and index lookups).
struct Tuple {
    std::vector<Value> values;
    RecordId rid;  // Where this tuple lives on disk (if applicable).

    Tuple() = default;
    explicit Tuple(std::vector<Value> vals) : values(std::move(vals)) {}
    Tuple(std::vector<Value> vals, RecordId r) : values(std::move(vals)), rid(r) {}

    const Value& get_value(size_t col_idx) const {
        if (col_idx >= values.size()) {
            throw std::out_of_range("Column index out of range");
        }
        return values[col_idx];
    }

    size_t size() const { return values.size(); }
};

// ─── Page Constants ──────────────────────────────────────────
static constexpr uint32_t PAGE_SIZE = 4096;  // 4 KB pages

}  // namespace minidb
