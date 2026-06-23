#pragma once

#include <cstdint>
#include <string>
#include <variant>

// Core type aliases and small value types shared across every layer of MiniDB.
namespace minidb {

// The unit of transfer between disk and memory. Fixed at 4 KiB to match the
// course material (SQLite's default page size) and the typical OS page size,
// so one logical page maps cleanly onto one OS page / disk block.
constexpr std::size_t PAGE_SIZE = 4096;

using PageId = std::int32_t;   // index of a page within a database file
using lsn_t  = std::uint64_t;  // log sequence number (used from M5 onward)
using TxId   = std::uint64_t;  // transaction id (used from M4 onward)

constexpr PageId INVALID_PAGE_ID = -1;
constexpr lsn_t  INVALID_LSN     = 0;
constexpr TxId   INVALID_TXID    = 0;

// A record identifier: which page, and which slot inside that page's slot array.
// The slot index is stable for the life of a record even if its bytes are moved
// by in-page compaction, because outside references use the slot, never the byte
// offset. This is what lets indexes store a fixed RID per row.
struct RID {
    PageId       page_id = INVALID_PAGE_ID;
    std::int16_t slot    = -1;

    bool operator==(const RID& o) const { return page_id == o.page_id && slot == o.slot; }
    bool operator!=(const RID& o) const { return !(*this == o); }
};

// Column value types supported by MiniDB. Used by the catalog/record codec
// (M2) and everything above it; the raw storage layer (M1) treats records as
// opaque byte strings and is unaware of these.
enum class ValueType { INT, VARCHAR, DOUBLE };

// A single column value. INT -> int64_t, VARCHAR -> std::string, DOUBLE -> double.
using Value = std::variant<std::int64_t, std::string, double>;

} // namespace minidb
