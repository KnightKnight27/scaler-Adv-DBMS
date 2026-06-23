// Disk manager: the only component that performs raw page I/O.
//
// Maps a logical page id to a byte offset (page_id * PAGE_SIZE) inside a single
// database file, and allocates new page ids by extending the file. Everything
// above it speaks in page ids and never touches the OS file directly.
#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

#include "common/types.hpp"
#include "storage/page.hpp"

namespace minidb {

class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    PageId allocate_page();                              // grow file by one page
    void read_page(PageId pid, uint8_t* dst);            // dst must be PAGE_SIZE
    void write_page(PageId pid, const uint8_t* src);     // src must be PAGE_SIZE
    void sync();

    PageId num_pages() const { return num_pages_; }

private:
    std::string   db_file_;
    std::fstream  file_;
    PageId        num_pages_ = 0;
    std::mutex    mtx_;
};

}  // namespace minidb
