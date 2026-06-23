#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include "storage/page.h"

class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    void ReadPage(PageId_t page_id, char* page_data);
    void WritePage(PageId_t page_id, const char* page_data);
    PageId_t AllocatePage();
    void DeallocatePage(PageId_t page_id);
    int GetNumPages() const { return num_pages_; }
    void ShutDown();
    void Clear(); // To wipe the DB for fresh runs/tests

private:
    std::string db_file_name_;
    std::fstream db_io_;
    int num_pages_ = 0;
    mutable std::mutex db_io_mutex_;
};
