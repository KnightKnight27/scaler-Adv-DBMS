#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace minidb {

// ── Size constants ──────────────────────────────────────────
constexpr uint32_t PAGE_SIZE       = 4096;      // 4 KB pages
constexpr uint32_t BUFFER_POOL_SIZE = 64;       // number of frames
constexpr uint32_t BATCH_SIZE      = 512;       // rows per batch (Track A)
constexpr uint32_t INVALID_PAGE_ID = UINT32_MAX;

// ── ID types ────────────────────────────────────────────────
using page_id_t  = uint32_t;
using frame_id_t = uint32_t;
using txn_id_t   = uint32_t;
using lsn_t      = uint64_t;
using slot_id_t  = uint16_t;

// ── Record identifier (page + slot) ─────────────────────────
struct RecordId {
    page_id_t page_id = INVALID_PAGE_ID;
    slot_id_t slot_id = 0;

    bool operator==(const RecordId& o) const {
        return page_id == o.page_id && slot_id == o.slot_id;
    }
    bool operator!=(const RecordId& o) const { return !(*this == o); }
};

// ── Value type (each cell in a row) ─────────────────────────
// Supports int, double, string
using Value = std::variant<int64_t, double, std::string>;

// ── Row = vector of values ──────────────────────────────────
using Row = std::vector<Value>;

// ── Column types ────────────────────────────────────────────
enum class ColumnType { INT, DOUBLE, STRING };

struct ColumnDef {
    std::string name;
    ColumnType  type;
};

using Schema = std::vector<ColumnDef>;

// ── Transaction state ───────────────────────────────────────
enum class TxnState { ACTIVE, COMMITTED, ABORTED };

// ── Lock modes ──────────────────────────────────────────────
enum class LockMode { SHARED, EXCLUSIVE };

// ── Comparison operators for predicates ─────────────────────
enum class CmpOp { EQ, NEQ, LT, LTE, GT, GTE };

} // namespace minidb
