#pragma once

#include <fstream>
#include <string>

#include "../common/types.hpp"

// The DiskManager is the only component that touches the data file. It reads
// and writes whole pages by page id, and grows the file one page at a time.
// Everything above it (buffer pool, heap, index) speaks in page ids, never
// file offsets — this is the single boundary between memory and disk.
class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    // Read PAGE_SIZE bytes of page `id` into `dest` (caller-owned, >= PAGE_SIZE).
    // Reading a not-yet-written page yields a zero-filled buffer.
    void read_page(PageID id, char* dest);

    // Overwrite page `id` with PAGE_SIZE bytes from `src`, then flush to the OS.
    void write_page(PageID id, const char* src);

    // Append a fresh, zero-filled page to the end of the file; returns its id.
    PageID allocate_page();

    PageID num_pages() const { return num_pages_; }

private:
    std::string  file_name_;
    std::fstream io_;
    PageID       num_pages_ = 0;
};
