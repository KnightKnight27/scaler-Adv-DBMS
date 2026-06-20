#pragma once
#include <mutex>
#include <string>
#include "common/config.h"
#include "common/types.h"

namespace minidb {

// The DiskManager is the only component that touches the database file. It maps
// page ids to byte offsets (offset = page_id * PAGE_SIZE) and performs
// fixed-size reads and writes via POSIX pread/pwrite. We use raw file
// descriptors rather than std::fstream because mixing reads and writes on a
// stream with seek forces costly buffer flushes; pread/pwrite are single,
// offset-addressed syscalls with no stream-level buffering.
class DiskManager {
public:
    // `truncate` starts the data file empty (the engine rebuilds page contents
    // from the WAL on startup, so the backing file is treated as scratch).
    explicit DiskManager(const std::string &db_file, bool truncate = false);
    ~DiskManager();

    void write_page(page_id_t page_id, const char *page_data);
    void read_page(page_id_t page_id, char *page_data);

    // Allocate a fresh page id (does not touch disk until first write).
    page_id_t allocate_page();

    // Force buffered writes to stable storage (explicit durability point).
    void sync();

    int num_pages() const { return num_pages_; }
    int write_count() const { return num_writes_; } // for benchmarks

private:
    std::string file_name_;
    int         fd_{-1};
    int         num_pages_{0};
    int         num_writes_{0};
    std::mutex  latch_;
};

} // namespace minidb
