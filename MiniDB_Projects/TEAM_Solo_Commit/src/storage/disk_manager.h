// MiniDB - DiskManager: reads and writes fixed 4KB pages to a single database file.
// This is the only component that touches the OS file directly (the same open/read/write/
// fsync path explored in Lab 1). Everything above it speaks in page ids.
#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

#include "page.h"

namespace minidb {

class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    // Read/write the 4KB block for `page_id`. Reading past the end yields a zeroed page.
    void ReadPage(int page_id, char* dest);
    void WritePage(int page_id, const char* src);

    // Grow the file by one page and return its id.
    int AllocatePage();

    int NumPages() const { return num_pages_; }
    void Sync();      // flush OS buffers to disk (durability point)
    void Truncate();  // reset the data file to empty (used when rebuilding from the WAL)

private:
    std::string file_name_;
    std::fstream io_;
    int num_pages_ = 0;
    std::mutex latch_;
};

}  // namespace minidb
