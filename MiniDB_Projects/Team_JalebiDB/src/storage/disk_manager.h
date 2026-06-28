#pragma once

#include "common/config.h"
#include "common/types.h"
#include <fstream>
#include <string>
#include <mutex>

namespace minidb {

class DiskManager {
public:
    explicit DiskManager(const std::string &db_file);
    ~DiskManager();

    // Write a page to disk
    void WritePage(page_id_t page_id, const char *page_data);

    // Read a page from disk
    void ReadPage(page_id_t page_id, char *page_data);

    // Allocate a new page on disk
    page_id_t AllocatePage();

    // Get number of pages currently on disk
    page_id_t GetNumPages() const { return num_pages_; }

    // Write log buffer to disk log file
    void WriteLog(const char *log_data, int size);

    // Read log records from disk log file
    int ReadLog(char *log_data, int size, int offset);

    // Get current log file size
    int GetLogFileSize();

    // Clear logs (for database restart/truncate if needed)
    void ClearLog();

    // Force flush database and log files to disk
    void ShutDown();

private:
    std::string db_file_name_;
    std::string log_file_name_;
    std::fstream db_io_;
    std::fstream log_io_;
    page_id_t num_pages_{0};
    std::mutex db_io_lck_;
};

} // namespace minidb
