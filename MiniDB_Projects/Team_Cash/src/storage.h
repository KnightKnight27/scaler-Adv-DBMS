// Storage engine: how rows become bytes and how those bytes are laid out on
// disk and cached in memory.
//
//   DiskManager   raw fixed-size (4 KB) page reads/writes against one file
//   Page          a slotted page: a slot directory plus packed tuple bytes
//   BufferPool    an in-memory cache of pages with LRU eviction
//   HeapFile      a table's rows spread across many pages, via the pool
#pragma once

#include <cstdint>
#include <fstream>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "types.h"

namespace minidb {

constexpr int PAGE_SIZE = 4096;
constexpr int HEADER_SIZE = 4;  // num_slots (2 bytes) + free_ptr (2 bytes)
constexpr int SLOT_SIZE = 4;    // offset (2 bytes) + length (2 bytes)

// Row <-> bytes. Self-describing so a page can be decoded on its own:
//   INT  -> 0x01 + 8 bytes (big-endian, signed)
//   TEXT -> 0x02 + 2-byte length + UTF-8 bytes
std::string encodeRow(const Row& row);
Row decodeRow(const std::string& bytes);

// One 4 KB slotted page. The slot directory grows left-to-right just after
// the header; tuple bytes are packed from the end of the page backwards.
class Page {
public:
    explicit Page(const std::string& bytes);  // load existing page
    static Page empty();                        // a fresh, formatted page

    int numSlots() const;
    int freeSpace() const;
    int insert(const std::string& record);      // returns slot id, or -1 if full
    bool get(int slot, std::string& out) const;  // false if slot is deleted
    void erase(int slot);                         // tombstone (length -> 0)

    const std::string& bytes() const { return data_; }

private:
    std::string data_;  // exactly PAGE_SIZE bytes

    void setNumSlots(int n);
    int freePtr() const;
    void setFreePtr(int p);
    void slot(int i, int& offset, int& length) const;
    void setSlot(int i, int offset, int length);
};

// Reads and writes whole 4 KB pages to one file, addressed by page number.
class DiskManager {
public:
    explicit DiskManager(const std::string& path);
    int numPages();
    std::string readPage(int pageId);
    void writePage(int pageId, const std::string& data);
    int allocatePage();  // appends a formatted empty page, returns its id

private:
    std::string path_;
    std::fstream f_;
};

// LRU cache of pages. The front of lru_ is the least-recently-used page;
// fetch() moves a page to the back, eviction drops from the front, and dirty
// pages are written back on eviction. Single-threaded, so no pin counts.
class BufferPool {
public:
    BufferPool(DiskManager* disk, int capacity = 16);
    Page* fetch(int pageId);
    void markDirty(int pageId);
    void flushAll();

    struct Stats { long hits = 0, misses = 0, evictions = 0; } stats;

private:
    DiskManager* disk_;
    int capacity_;
    std::unordered_map<int, Page> frames_;
    std::list<int> lru_;  // front = LRU, back = MRU
    std::unordered_map<int, std::list<int>::iterator> pos_;
    std::unordered_set<int> dirty_;

    void touch(int pageId);
    void evictOne();
};

// A table's rows, stored across the pages of one heap file.
class HeapFile {
public:
    HeapFile(DiskManager* disk, BufferPool* pool) : disk_(disk), pool_(pool) {}

    RID insert(const std::string& record);
    bool get(const RID& rid, std::string& out);
    void erase(const RID& rid);

    DiskManager* disk() { return disk_; }
    BufferPool* pool() { return pool_; }

private:
    DiskManager* disk_;
    BufferPool* pool_;
};

}  // namespace minidb
