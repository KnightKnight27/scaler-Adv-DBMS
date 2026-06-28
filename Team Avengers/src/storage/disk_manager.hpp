// ============================================================================
//  disk_manager.hpp — The only component that touches the data file.
//
//  Responsibility (and nothing more): move exactly PAGE_SIZE bytes between a
//  caller-owned buffer and offset (page_id * PAGE_SIZE) in a single file.
//  It does NOT cache, interpret, or lock pages — that is the buffer pool's job.
//  Keeping disk I/O behind this one wall is what lets the rest of the engine
//  be tested without a real disk later.
// ============================================================================
#pragma once

#include "../common/types.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <mutex>

namespace minidb {

class DiskManager {
public:
    explicit DiskManager(const std::string& db_file) : file_name_(db_file) {
        // Open for binary read+write. We must open WITHOUT trunc so an existing
        // database survives a restart (that is what makes recovery meaningful).
        db_io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
        if (!db_io_.is_open()) {
            // File does not exist yet: create it, then reopen r/w.
            db_io_.clear();
            db_io_.open(db_file, std::ios::binary | std::ios::trunc | std::ios::out);
            db_io_.close();
            db_io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
            if (!db_io_.is_open())
                throw std::runtime_error("DiskManager: cannot open " + db_file);
        }
    }

    ~DiskManager() {
        if (db_io_.is_open()) db_io_.close();
    }

    // Write one page. The caller guarantees `data` is at least PAGE_SIZE bytes.
    void write_page(page_id_t page_id, const char* data) {
        std::lock_guard<std::mutex> g(io_latch_);
        size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;
        db_io_.seekp(offset);
        db_io_.write(data, PAGE_SIZE);
        if (db_io_.bad())
            throw std::runtime_error("DiskManager: write_page failed");
        // flush so the bytes actually reach the OS — required before we can
        // claim a page (or a WAL record) is durable.
        db_io_.flush();
        ++num_writes_;
    }

    // Read one page into `data` (PAGE_SIZE bytes). Reading past EOF (a page that
    // was allocated but never written) yields zeros rather than an error.
    void read_page(page_id_t page_id, char* data) {
        std::lock_guard<std::mutex> g(io_latch_);
        size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;
        db_io_.seekg(0, std::ios::end);
        size_t file_size = static_cast<size_t>(db_io_.tellg());
        if (offset >= file_size) {
            // Not yet on disk: hand back a zeroed page.
            std::fill(data, data + PAGE_SIZE, 0);
            return;
        }
        db_io_.seekg(offset);
        db_io_.read(data, PAGE_SIZE);
        // A short read (partial last page) is padded with zeros.
        std::streamsize got = db_io_.gcount();
        if (got < PAGE_SIZE) std::fill(data + got, data + PAGE_SIZE, 0);
        db_io_.clear();  // clear any eofbit so the stream stays usable
    }

    // Hand out the next never-before-used page id. Pure bump allocator: we do
    // not reuse ids here — page reclamation is left to the free-space map.
    page_id_t allocate_page() { return next_page_id_++; }

    int num_writes() const { return num_writes_; }
    page_id_t page_count() const { return next_page_id_; }

private:
    std::string  file_name_;
    std::fstream db_io_;
    std::mutex   io_latch_;          // serialises seek+rw, which are stateful
    page_id_t    next_page_id_ = 0;
    int          num_writes_ = 0;    // benchmark counter
};

}  // namespace minidb
