#pragma once
#include <fstream>
#include <string>
#include <mutex>
#include "storage/page.h"

namespace minidb {

class DiskManager {
public:
    // Create a new DiskManager that opens or creates the file
    explicit DiskManager(const std::string& file_name);
    ~DiskManager();

    // Write a page to the disk
    void WritePage(PageId page_id, const char* page_data);

    // Read a page from the disk
    void ReadPage(PageId page_id, char* page_data);

    // Get the next free page ID (simply appends to file)
    PageId AllocatePage();

    // Returns the size of the file in bytes
    int GetFileSize();

private:
    std::string file_name_;
    std::fstream db_io_;
    std::mutex db_io_mutex_; // Thread safety for concurrent file IO
    PageId next_page_id_;
};

} // namespace minidb
