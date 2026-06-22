// ============================================================================
// types.h  --  Fundamental type definitions shared across all MiniDB modules.
//
// Every other layer (storage, index, execution, ...) depends on the small set
// of identifiers and constants declared here.  Keeping them in one place means
// there is a single, authoritative definition of "what is a page id", "how big
// is a page", and so on.  This is the file to read first.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
using namespace std;

namespace minidb {

// ---------------------------------------------------------------------------
// On-disk geometry
// ---------------------------------------------------------------------------
// A "page" is the fixed-size unit of I/O between disk and memory.  Real systems
// use 4 KB, 8 KB or 16 KB pages.  We use 4 KB.  The entire storage engine reads
// and writes whole pages -- never individual bytes -- because disks (and the OS
// page cache) operate on block-sized chunks.
constexpr int PAGE_SIZE = 4096;

// How many frames (page-sized slots) the buffer pool holds in memory at once.
// Small on purpose so that eviction actually happens during demos.
constexpr int BUFFER_POOL_SIZE = 64;

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------
// A page id is just the index of the page within its file: page 0 is the first
// 4 KB, page 1 the next 4 KB, and so on.  byte offset = page_id * PAGE_SIZE.
using page_id_t = int32_t;

// A logical sequence number for WAL records (monotonically increasing).
using lsn_t = int64_t;

// Transaction id.
using txn_id_t = int32_t;

// Sentinel meaning "no such page".
constexpr page_id_t INVALID_PAGE_ID = -1;
constexpr lsn_t      INVALID_LSN     = -1;
constexpr txn_id_t   INVALID_TXN_ID  = -1;

// ---------------------------------------------------------------------------
// RID -- Record IDentifier
// ---------------------------------------------------------------------------
// Every tuple physically lives in some slot of some page.  A RID names that
// location: (page_id, slot_number).  Indexes map a key -> RID, and the heap
// file uses RIDs to fetch the actual bytes.  Think of it as the tuple's
// physical address.
struct RID {
  page_id_t page_id{INVALID_PAGE_ID};
  int32_t   slot{-1};

  RID() = default;
  RID(page_id_t p, int32_t s) : page_id(p), slot(s) {}

  bool operator==(const RID &o) const {
    return page_id == o.page_id && slot == o.slot;
  }
  bool operator!=(const RID &o) const { return !(*this == o); }
  bool operator<(const RID &o) const {
    return page_id != o.page_id ? page_id < o.page_id : slot < o.slot;
  }

  string toString() const {
    return "(" + to_string(page_id) + "," + to_string(slot) + ")";
  }
};

}  // namespace minidb
