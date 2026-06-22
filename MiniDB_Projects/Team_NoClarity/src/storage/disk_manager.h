#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include "common/config.h"
#include <string>
#include <fstream>
#include <mutex>

#include <unordered_map>

namespace minidb {

class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    void ReadPage(page_id_t page_id, char* page_data);
    void WritePage(page_id_t page_id, const char* page_data, lsn_t lsn = 0);
    page_id_t AllocatePage();

    lsn_t GetPageLSN(page_id_t page_id);
    void SetPageLSN(page_id_t page_id, lsn_t lsn);
    void LoadLSNs();
    void SaveLSNs();

private:
    std::string db_file_name_;
    std::fstream db_io_;
    page_id_t num_pages_{0};
    std::unordered_map<page_id_t, lsn_t> page_lsns_;
    std::mutex latch_;
};

} // namespace minidb

#endif // DISK_MANAGER_H
