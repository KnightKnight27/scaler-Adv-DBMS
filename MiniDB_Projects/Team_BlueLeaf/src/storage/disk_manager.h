#pragma once

#include <cstddef>
#include <string>

#include "common/types.h"

namespace minidb {

// DiskManager owns the single database file and does all page-granular I/O.
// The database is one file divided into fixed-size pages (the SQLite model
// taught in class), addressed by PageId = byte offset / PAGE_SIZE.
//
// It is also the integrity boundary: every page carries a CRC32 in its first 4
// bytes, stamped on write and verified on read, so a torn or corrupted page is
// detected rather than silently returned.
class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    // Read page `pid` into `out` (PAGE_SIZE bytes). Throws DBException on a
    // checksum mismatch or an out-of-range page.
    void read_page(PageId pid, char* out);

    // Write `in` (PAGE_SIZE bytes) to page `pid`, stamping its checksum first.
    void write_page(PageId pid, const char* in);

    // Grow the file by one zero-initialised page and return its id.
    PageId allocate_page();

    std::size_t num_pages() const { return num_pages_; }

    // Force buffered writes to durable storage (used by WAL/flush in later milestones).
    void sync();

private:
    std::string file_name_;
    int         fd_;
    std::size_t num_pages_;
};

} // namespace minidb
