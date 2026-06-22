#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "value.h"

// ─── Constants ──────────────────────────────────────────────────────────────
constexpr uint32_t PAGE_SIZE   = 4096;       // bytes per page
constexpr uint32_t INVALID_PID = UINT32_MAX; // sentinel "no page"
using PageId = uint32_t;

// ─── Page ───────────────────────────────────────────────────────────────────
//
// Slotted-page layout (all multiples of 1 byte):
//
//  [0..1]  num_slots   : number of slot entries
//  [2..3]  free_end    : byte offset where free space ends (records grow ←)
//  [4..]   slot_array  : SlotEntry[num_slots], growing →
//            ...free space...
//  [...PAGE_SIZE-1] records, growing ←
//
// A deleted slot has offset==0 && length==0 (tombstone).
// ─────────────────────────────────────────────────────────────────────────────

struct SlotEntry {
    uint16_t offset;  // byte offset from page start to the record
    uint16_t length;  // record length in bytes; 0 means deleted
};

class Page {
public:
    std::array<uint8_t, PAGE_SIZE> data{};
    bool dirty = false; // buffer pool uses this to decide whether to write back

    // Header field accessors — cast into raw bytes for zero-copy reads/writes
    uint16_t  num_slots()  const { return read16(0); }
    uint16_t& num_slots()        { return ref16(0);  }
    uint16_t  free_end()   const { return read16(2); }
    uint16_t& free_end()         { return ref16(2);  }

    const SlotEntry* slot_array() const {
        return reinterpret_cast<const SlotEntry*>(data.data() + 4);
    }
    SlotEntry* slot_array() {
        return reinterpret_cast<SlotEntry*>(data.data() + 4);
    }

    // Reset page to an empty, freshly-allocated state
    void init() {
        data.fill(0);
        free_end() = PAGE_SIZE; // records start from the back
    }

    // Free bytes remaining between the slot array end and the record area.
    // Guard against underflow: a malformed page (free_end < slot_area_end)
    // reports zero free space rather than a huge wrapped-around value.
    uint16_t free_space() const {
        uint16_t slot_area_end = static_cast<uint16_t>(4 + num_slots() * sizeof(SlotEntry));
        if (free_end() < slot_area_end) return 0;
        return free_end() - slot_area_end;
    }

    // Append a record; returns the assigned slot id, or UINT16_MAX if no room
    uint16_t insert(const uint8_t* rec, uint16_t len) {
        if (free_space() < len + static_cast<uint16_t>(sizeof(SlotEntry)))
            return UINT16_MAX;

        free_end() -= len;
        std::memcpy(data.data() + free_end(), rec, len);

        uint16_t sid = num_slots()++;
        slot_array()[sid] = {free_end(), len};
        dirty = true;
        return sid;
    }

    // Read record at slot into out; returns false for deleted / out-of-range slots
    bool read(uint16_t slot, std::vector<uint8_t>& out) const {
        if (slot >= num_slots()) return false;
        const SlotEntry& s = slot_array()[slot];
        if (s.length == 0) return false; // tombstone
        out.assign(data.data() + s.offset, data.data() + s.offset + s.length);
        return true;
    }

    // Mark a slot as deleted (tombstone); space is not reclaimed for simplicity
    void remove(uint16_t slot) {
        if (slot < num_slots()) {
            slot_array()[slot] = {0, 0};
            dirty = true;
        }
    }

private:
    uint16_t  read16(int off) const { uint16_t v; std::memcpy(&v, data.data()+off, 2); return v; }
    uint16_t& ref16(int off)        { return *reinterpret_cast<uint16_t*>(data.data()+off); }
};

// ─── DiskManager ─────────────────────────────────────────────────────────────
//
// Thin wrapper around a file on disk: reads and writes fixed-size pages.
// Each table has its own file named "<table_name>.db".
// ─────────────────────────────────────────────────────────────────────────────
class DiskManager {
public:
    explicit DiskManager(const std::string& filepath);
    ~DiskManager();

    void     read_page(PageId pid, Page& page);
    void     write_page(PageId pid, const Page& page);
    PageId   allocate_page(); // extends the file by one page; returns new page id
    uint32_t page_count() const { return page_count_; }

private:
    std::fstream file_;
    uint32_t     page_count_ = 0;
};

// ─── BufferPool ──────────────────────────────────────────────────────────────
//
// Fixed-size in-memory page cache with LRU eviction.
//
// Usage:
//   Page* p = bp.fetch(pid);   // pin page (won't be evicted while pinned)
//   ...modify p...
//   bp.unpin(pid, dirty=true); // allow eviction; dirty=true triggers write-back
// ─────────────────────────────────────────────────────────────────────────────
class BufferPool {
public:
    explicit BufferPool(DiskManager& dm, size_t pool_size = 64);

    Page* fetch(PageId pid);          // load page into pool, increment pin count
    void  unpin(PageId pid, bool dirty = false);
    void  flush_all();                // write all dirty pages back to disk

    size_t pool_size() const { return frames_.size(); }

private:
    struct Frame {
        Page     page;
        PageId   pid      = INVALID_PID;
        int      pin_cnt  = 0;
        bool     dirty    = false;
    };

    // Evict one unpinned frame using LRU order. Throws if all frames pinned.
    size_t evict();

    DiskManager&              dm_;
    std::vector<Frame>        frames_;
    std::unordered_map<PageId, size_t> page_to_frame_; // pid → frame index
    std::list<size_t>         lru_list_;               // front = most recently used
    std::unordered_map<size_t, std::list<size_t>::iterator> lru_pos_;
};

// ─── Serialization helpers ────────────────────────────────────────────────────
// Used by HeapTable to encode/decode a Row (vector<Value>) to/from raw bytes.
// Format per value:
//   [1B type][1B is_null]
//   If INT and not null: [8B int64_t LE]
//   If VARCHAR and not null: [4B length][<length> bytes]
// ─────────────────────────────────────────────────────────────────────────────
namespace serde {
std::vector<uint8_t>  encode(const std::vector<Value>& row);
std::vector<Value>    decode(const uint8_t* buf, size_t len, const std::vector<Type>& types);
} // namespace serde
