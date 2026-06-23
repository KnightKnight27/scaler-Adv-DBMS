#pragma once
// storage/heap_file.h — Heap file: an unordered collection of pages on disk.
//
// File layout:
//   bytes 0-3:       num_pages (uint32_t)
//   bytes 4..4+PAGE_SIZE-1:      page 0
//   bytes 4+PAGE_SIZE..4+2*PAGE_SIZE-1: page 1
//   ...
//
// A free-space map (in memory) tracks how many free bytes each page has,
// so insert can quickly find a page with room.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "common/types.h"
#include "storage/page.h"

namespace minidb {

class HeapFile {
public:
    // Opens or creates the heap file at `file_path`.
    explicit HeapFile(const std::string& file_path);
    ~HeapFile();

    // ── Page management ──
    page_id_t allocate_page();
    void      read_page(page_id_t pid, Page& page);
    void      write_page(page_id_t pid, const Page& page);

    // ── Tuple-level convenience API ──
    RecordId insert_tuple(const char* data, uint16_t length);
    bool     delete_tuple(RecordId rid);
    bool     get_tuple(RecordId rid, char* out, uint16_t* out_length);

    // ── Metadata ──
    page_id_t get_num_pages() const { return num_pages_; }

    // Re-scan all pages and rebuild the in-memory free-space map.
    void rebuild_free_space_map();

private:
    std::string           file_path_;
    std::fstream          file_;
    page_id_t             num_pages_;
    std::vector<uint16_t> free_space_map_;   // free_space_map_[pid] = free bytes

    // File header is just 4 bytes (num_pages).
    static constexpr uint32_t FILE_HEADER_SIZE = 4;

    // Byte offset in the file where page `pid` starts.
    std::streamoff page_offset(page_id_t pid) const {
        return FILE_HEADER_SIZE + static_cast<std::streamoff>(pid) * PAGE_SIZE;
    }

    // Write the 4-byte header (num_pages) to the file.
    void write_header();
    void read_header();
};

}  // namespace minidb
