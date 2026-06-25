#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <stdexcept>

//  MVCC Version Chains Manager — Core Types & Constants
//  Supports: NSM (N-ary Storage Model / Row Store),
//            DSM (Decomposition Storage Model / Column Store),
//            PAX (Partition Attributes Across)

namespace mvcc {

// --------------- Transaction & Timestamp Types ---------------
using TxnID      = uint64_t;
using Timestamp  = uint64_t;
using PageID     = uint32_t;
using SlotID     = uint32_t;
using ColumnID   = uint16_t;

constexpr TxnID    INVALID_TXN_ID  = 0;
constexpr Timestamp INF_TS         = UINT64_MAX;   // "infinity" end timestamp
constexpr PageID   INVALID_PAGE_ID = UINT32_MAX;
constexpr size_t   PAGE_SIZE       = 4096;         // 4 KB pages

// --------------- Transaction Isolation Levels ---------------
enum class IsolationLevel : uint8_t {
    READ_UNCOMMITTED = 0,
    READ_COMMITTED   = 1,
    REPEATABLE_READ  = 2,
    SNAPSHOT         = 3,
    SERIALIZABLE     = 4
};

// --------------- Page Layout Types ---------------
enum class PageLayout : uint8_t {
    NSM = 0,   // Row-oriented (N-ary Storage Model)
    DSM = 1,   // Column-oriented (Decomposition Storage Model)
    PAX = 2    // Hybrid (Partition Attributes Across)
};

inline std::string pageLayoutName(PageLayout layout) {
    switch (layout) {
        case PageLayout::NSM: return "NSM (Row Store)";
        case PageLayout::DSM: return "DSM (Column Store)";
        case PageLayout::PAX: return "PAX (Hybrid)";
    }
    return "Unknown";
}

// --------------- MVCC Version Status ---------------
enum class VersionStatus : uint8_t {
    ACTIVE    = 0,   // Currently active version
    COMMITTED = 1,   // Committed and visible
    ABORTED   = 2,   // Rolled back / invisible
    DELETED   = 3    // Logically deleted
};

// --------------- Column Data Types ---------------
enum class DataType : uint8_t {
    INT32   = 0,
    INT64   = 1,
    FLOAT   = 2,
    DOUBLE  = 3,
    VARCHAR = 4,
    BOOL    = 5
};

inline size_t fixedSize(DataType dt) {
    switch (dt) {
        case DataType::INT32:  return 4;
        case DataType::INT64:  return 8;
        case DataType::FLOAT:  return 4;
        case DataType::DOUBLE: return 8;
        case DataType::BOOL:   return 1;
        default:               return 0;   // VARCHAR is variable
    }
}

// --------------- Schema Definition ---------------
struct ColumnDef {
    std::string name;
    DataType    type;
    size_t      maxLen;   // for VARCHAR; 0 otherwise
    bool        nullable;

    ColumnDef(std::string n, DataType t, size_t ml = 0, bool nullable = false)
        : name(std::move(n)), type(t), maxLen(ml), nullable(nullable) {}
};

struct TableSchema {
    std::string             tableName;
    std::vector<ColumnDef>  columns;
    ColumnID                primaryKeyCol = 0;

    size_t numColumns() const { return columns.size(); }
};

// --------------- Raw Value Holder ---------------
struct Value {
    DataType type;
    bool     isNull = false;
    union {
        int32_t  i32;
        int64_t  i64;
        float    f32;
        double   f64;
        bool     b;
    } num{};
    std::string str;   // for VARCHAR

    static Value makeInt32 (int32_t  v)  { Value r; r.type = DataType::INT32;  r.num.i32 = v; return r; }
    static Value makeInt64 (int64_t  v)  { Value r; r.type = DataType::INT64;  r.num.i64 = v; return r; }
    static Value makeFloat (float    v)  { Value r; r.type = DataType::FLOAT;  r.num.f32 = v; return r; }
    static Value makeDouble(double   v)  { Value r; r.type = DataType::DOUBLE; r.num.f64 = v; return r; }
    static Value makeBool  (bool     v)  { Value r; r.type = DataType::BOOL;   r.num.b   = v; return r; }
    static Value makeVarchar(std::string v){ Value r; r.type = DataType::VARCHAR; r.str = std::move(v); return r; }
    static Value makeNull(DataType t)    { Value r; r.type = t; r.isNull = true; return r; }

    std::string toString() const {
        if (isNull) return "NULL";
        switch (type) {
            case DataType::INT32:   return std::to_string(num.i32);
            case DataType::INT64:   return std::to_string(num.i64);
            case DataType::FLOAT:   return std::to_string(num.f32);
            case DataType::DOUBLE:  return std::to_string(num.f64);
            case DataType::BOOL:    return num.b ? "true" : "false";
            case DataType::VARCHAR: return str;
        }
        return "?";
    }

    bool operator==(const Value& o) const {
        if (type != o.type || isNull != o.isNull) return false;
        if (isNull) return true;
        switch (type) {
            case DataType::INT32:   return num.i32 == o.num.i32;
            case DataType::INT64:   return num.i64 == o.num.i64;
            case DataType::FLOAT:   return num.f32 == o.num.f32;
            case DataType::DOUBLE:  return num.f64 == o.num.f64;
            case DataType::BOOL:    return num.b   == o.num.b;
            case DataType::VARCHAR: return str     == o.str;
        }
        return false;
    }
    bool operator!=(const Value& o) const { return !(*this == o); }
};

// A tuple = ordered list of Values
using Tuple = std::vector<Value>;

// --------------- Record Identifier ---------------
struct RID {
    PageID page;
    SlotID slot;
    bool operator==(const RID& o) const { return page == o.page && slot == o.slot; }
};

// --------------- MVCC exceptions ---------------
struct TxnConflictError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct TxnAbortedError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} 