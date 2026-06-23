#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include "common/config.h"
#include <string>
#include <fstream>
#include <mutex>

#include <unordered_map>

namespace minidb {

/**
 * Manages database file layout, loading, persisting, and mapping pages on physical storage.
 */
class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    // Pulls binary 4KB block from physical file into frame buffer
    void ReadPage(page_id_t page_id, char* page_data);
    
    // Writes binary 4KB block back to database disk file location
    void WritePage(page_id_t page_id, const char* page_data, lsn_t lsn = 0);
    
    // Extends database physical allocation offset footprint by a page
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
