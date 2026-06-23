#pragma once
// ---------------------------------------------------------------------------
// common.h - shared types used across every MiniDB subsystem.
//
// A Value is one cell. A Row is a positional list of cells matching a Schema.
// An RID (record id) is the physical address of a tuple: which page, which slot.
// Keeping these tiny and shared is what lets the storage, index, executor and
// transaction layers all speak the same language.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include <stdexcept>

namespace minidb {

// Page size mirrors the 4 KB pages we saw in SQLite during Lab 2/Lab 4.
constexpr int PAGE_SIZE = 4096;
constexpr int INVALID_PAGE_ID = -1;

using TxId = uint64_t;
constexpr TxId INVALID_TX = 0;

// MiniDB supports two column types: 64-bit integers and variable-length text.
enum class Type { INT, TEXT };

// A single cell value. variant keeps it type-safe without inheritance.
using Value = std::variant<int64_t, std::string>;

struct Column {
    std::string name;
    Type        type;
    bool        is_primary_key = false;
};

using Schema = std::vector<Column>;

// A row is positional: row[i] corresponds to schema[i].
using Row = std::vector<Value>;

// Record identifier - the physical location of a tuple on disk.
struct RID {
    int page_id = INVALID_PAGE_ID;
    int slot    = -1;

    bool valid() const { return page_id != INVALID_PAGE_ID && slot >= 0; }
    bool operator==(const RID& o) const { return page_id == o.page_id && slot == o.slot; }
};

// ---- small helpers ---------------------------------------------------------

inline int64_t as_int(const Value& v) {
    if (auto p = std::get_if<int64_t>(&v)) return *p;
    throw std::runtime_error("value is not an INT");
}

inline const std::string& as_text(const Value& v) {
    if (auto p = std::get_if<std::string>(&v)) return *p;
    throw std::runtime_error("value is not TEXT");
}

inline std::string value_to_string(const Value& v) {
    if (auto p = std::get_if<int64_t>(&v)) return std::to_string(*p);
    return std::get<std::string>(v);
}

inline int schema_index(const Schema& s, const std::string& col) {
    for (int i = 0; i < (int)s.size(); ++i) if (s[i].name == col) return i;
    return -1;
}

} // namespace minidb
