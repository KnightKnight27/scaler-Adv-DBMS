#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// disk_manager.h  –  raw page I/O against a single .db file
// ─────────────────────────────────────────────────────────────────────────────
#include "common/config.h"
#include "common/types.h"
#include <string>
#include <fstream>
#include <mutex>

namespace minidb {

/**
 * DiskManager – page-level reads and writes.
 *
 * Layout: page N occupies bytes [N*PAGE_SIZE, (N+1)*PAGE_SIZE).
 * AllocatePage() returns the next unused page_id (monotonically increasing).
 * It does NOT write to disk; the buffer pool handles that.
 */
class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    void      WritePage(page_id_t page_id, const char* data);
    void      ReadPage (page_id_t page_id,       char* data);
    page_id_t AllocatePage();
    void      Flush();

    const std::string& FilePath() const { return file_name_; }

private:
    std::string  file_name_;
    std::fstream io_;
    std::mutex   latch_;
    page_id_t    next_page_id_{0};
};

} // namespace minidb
