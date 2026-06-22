#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include "common/config.h"
#include <string>
#include <fstream>
#include <mutex>

namespace minidb {

class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    void ReadPage(page_id_t page_id, char* page_data);
    void WritePage(page_id_t page_id, const char* page_data);
    page_id_t AllocatePage();

private:
    std::string db_file_name_;
    std::fstream db_io_;
    page_id_t num_pages_{0};
    std::mutex latch_;
};

} // namespace minidb

#endif // DISK_MANAGER_H
