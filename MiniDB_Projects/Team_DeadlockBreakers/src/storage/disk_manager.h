#pragma once

#include "common/types.h"
#include <fstream>
#include <string>
#include <mutex>

namespace minidb {

/*
 * DiskManager: handles low-level file I/O for a single database file.
 * Each table gets its own file. Pages are addressed by page_id (0-indexed).
 */
class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    // Read page_id's data into the provided buffer (must be PAGE_SIZE bytes)
    void ReadPage(page_id_t page_id, char* data);

    // Write buffer contents to page_id on disk
    void WritePage(page_id_t page_id, const char* data);

    // Allocate a new page, returns its page_id
    page_id_t AllocatePage();

    // Number of pages currently in the file
    page_id_t GetNumPages() const { return num_pages_; }

    const std::string& GetFileName() const { return file_name_; }

private:
    std::string  file_name_;
    std::fstream file_;
    page_id_t    num_pages_ = 0;
    std::mutex   io_mutex_;
};

} // namespace minidb
