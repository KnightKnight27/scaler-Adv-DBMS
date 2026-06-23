#pragma once
// ---------------------------------------------------------------------------
// disk_manager.h - the only component that actually touches the disk.
//
// MiniDB (like SQLite) keeps the whole database in ONE file made of fixed-size
// 4 KB pages laid end to end. Page N lives at byte offset N * PAGE_SIZE.
// The disk manager just reads and writes those raw page-sized blocks; it knows
// nothing about tuples, indexes or transactions. This is the bottom of the
// stack - the read()/write() syscall boundary we traced in Lab 1.
// ---------------------------------------------------------------------------
#include "../common.h"
#include <fstream>
#include <string>
#include <cstring>

namespace minidb {

class DiskManager {
public:
    explicit DiskManager(const std::string& db_file) : file_name_(db_file) {
        // Open existing file for read+write, or create it if missing.
        file_.open(db_file, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
            std::ofstream create(db_file, std::ios::binary);
            create.close();
            file_.open(db_file, std::ios::in | std::ios::out | std::ios::binary);
        }
        file_.seekg(0, std::ios::end);
        std::streampos bytes = file_.tellg();
        num_pages_ = (bytes <= 0) ? 0 : (int)(bytes / PAGE_SIZE);
    }

    ~DiskManager() { if (file_.is_open()) file_.close(); }

    int num_pages() const { return num_pages_; }

    // Grow the file by one page and return its id.
    int allocate_page() {
        int pid = num_pages_++;
        char empty[PAGE_SIZE];
        std::memset(empty, 0, PAGE_SIZE);
        file_.seekp((std::streamoff)pid * PAGE_SIZE, std::ios::beg);
        file_.write(empty, PAGE_SIZE);
        file_.flush();
        return pid;
    }

    void read_page(int page_id, char* dest) {
        file_.seekg((std::streamoff)page_id * PAGE_SIZE, std::ios::beg);
        file_.read(dest, PAGE_SIZE);
        // A short read (e.g. a freshly allocated page) is zero-filled.
        std::streamsize got = file_.gcount();
        if (got < PAGE_SIZE) std::memset(dest + got, 0, PAGE_SIZE - got);
        file_.clear();
    }

    void write_page(int page_id, const char* src) {
        file_.seekp((std::streamoff)page_id * PAGE_SIZE, std::ios::beg);
        file_.write(src, PAGE_SIZE);
        file_.flush(); // push to the OS; durability of committed data is WAL's job
    }

    const std::string& file_name() const { return file_name_; }

private:
    std::string  file_name_;
    std::fstream file_;
    int          num_pages_ = 0;
};

} // namespace minidb
