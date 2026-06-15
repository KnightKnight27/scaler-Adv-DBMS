#pragma once
// storage.h — Three-layer storage stack.
//
//  DiskManager  →  raw page I/O (4 KB pages in a single .db file)
//  BufferPool   →  LRU in-memory cache of pages
//  Heap         →  slotted-page row storage on top of the pool
//
// This layering matches textbook database architecture: the disk manager is
// the only code that touches the OS file; the buffer pool hides I/O latency
// from the rest of the engine; the heap provides record-level semantics.
#include "value.h"
#include <array>
#include <fstream>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

// ── Constants ─────────────────────────────────────────────────────────────────
constexpr int PAGE_SIZE = 4096;   // 4 KB, aligns with most OS VM page sizes

using Page = std::array<char, PAGE_SIZE>;

// ── DiskManager ───────────────────────────────────────────────────────────────
// Reads and writes fixed-size pages to/from a flat binary file.
// This is the ONLY component that calls the OS file API.
class DiskManager {
public:
    explicit DiskManager(const std::string& path);
    ~DiskManager();

    void read_page (int page_id, Page& out);
    void write_page(int page_id, const Page& p);
    int  new_page  ();                    // appends a blank page; returns its id
    int  page_count() const { return num_pages_; }

private:
    std::fstream file_;
    int          num_pages_ = 0;
};

// ── BufferPool ────────────────────────────────────────────────────────────────
// Keeps a fixed number of "frames" in memory.  When a needed page isn't cached
// (a miss), the pool evicts the Least-Recently-Used *unpinned* frame, writing
// it back to disk first if it was modified (dirty).
//
// Callers must call fetch() to get a pointer and unpin() when done.
// A pinned frame is never evicted—callers must not hold pins indefinitely.
struct Frame {
    Page page    = {};
    int  page_id = -1;   // -1 means this frame is unused
    bool dirty   = false; // needs write-back before eviction
    int  pins    = 0;     // reference count; 0 = evictable
};

class BufferPool {
public:
    explicit BufferPool(DiskManager& disk, int capacity = 64);
    ~BufferPool();   // flushes all dirty frames on shutdown

    Page* fetch(int page_id);              // pins and returns pointer
    void  unpin(int page_id, bool dirty);  // must be called after every fetch
    void  flush_all();

    long hits = 0, misses = 0;   // for benchmark/demo reporting

private:
    int  find_victim();                    // LRU eviction; -1 if all pinned
    void load(int frame_idx, int page_id); // bring page from disk into frame

    DiskManager&          disk_;
    std::vector<Frame>    frames_;
    // LRU list stores frame indices; front = most-recently-used
    std::list<int>        lru_;
    std::unordered_map<int,int>  page_to_frame_;   // page_id → frame index
    std::unordered_map<int, std::list<int>::iterator> lru_pos_; // frame → lru_ iterator
};

// ── Heap ──────────────────────────────────────────────────────────────────────
// Stores variable-length records across a collection of slotted pages.
//
// Slotted-page internal layout:
//   Bytes 0–3  : int32  — slot count (number of slots in the directory)
//   Bytes 4–7  : int32  — free-space pointer (offset of next free byte for records)
//   Bytes 8+   : slot directory — each entry is 8 bytes: (int32 offset, int32 length)
//                  length == -1  →  tombstone (row was deleted)
//   End of page: records packed from PAGE_SIZE downward
//
// Records grow from the end toward the middle; the slot directory grows from
// the beginning toward the middle.  A page is full when they would overlap.
class Heap {
public:
    Heap(BufferPool& pool, DiskManager& disk);

    RID  insert(const std::string& row);              // returns the assigned RID
    bool fetch (RID rid, std::string& out);           // false if tombstoned
    void remove(RID rid);                             // marks slot as tombstone
    std::vector<std::pair<RID, std::string>> scan();  // full sequential scan

private:
    int  find_or_alloc(int needed);   // find a page with enough free space

    // Typed accessors into raw page bytes — keeps the Heap logic readable.
    static int  slot_count(const Page& p);
    static int  free_top  (const Page& p);
    static void set_slot_count(Page& p, int n);
    static void set_free_top  (Page& p, int v);
    static void read_slot (const Page& p, int slot, int& off, int& len);
    static void write_slot(Page& p, int slot, int off, int len);

    BufferPool&      pool_;
    DiskManager&     disk_;
    std::vector<int> page_ids_;   // pages belonging to this heap, in order
};

} // namespace minidb
