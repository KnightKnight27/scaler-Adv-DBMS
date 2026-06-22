#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace minidb {

constexpr int PAGE_SIZE = 4096;

// Owns the single data file and moves fixed-size pages between disk and memory.
// A page lives at byte offset page_id * PAGE_SIZE.
class DiskManager {
public:
    explicit DiskManager(const std::string& path) : path_(path) {
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
            std::ofstream create(path_, std::ios::binary);
            create.close();
            file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        }
        recompute_num_pages();
    }

    int num_pages() const { return num_pages_; }

    void recompute_num_pages() {
        file_.clear();
        file_.seekg(0, std::ios::end);
        std::streamoff sz = file_.tellg();
        num_pages_ = sz <= 0 ? 0 : static_cast<int>(sz / PAGE_SIZE);
    }

    int allocate_page() {
        int pid = num_pages_++;
        std::vector<uint8_t> zero(PAGE_SIZE, 0);
        write_page(pid, zero.data());
        return pid;
    }

    void read_page(int page_id, uint8_t* buf) {
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
        file_.read(reinterpret_cast<char*>(buf), PAGE_SIZE);
        std::streamsize got = file_.gcount();
        if (got < PAGE_SIZE) {
            std::memset(buf + got, 0, PAGE_SIZE - static_cast<size_t>(got));
            file_.clear();
        }
    }

    void write_page(int page_id, const uint8_t* buf) {
        file_.clear();
        file_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
        file_.write(reinterpret_cast<const char*>(buf), PAGE_SIZE);
        file_.flush();
        if (page_id + 1 > num_pages_) num_pages_ = page_id + 1;
    }

private:
    std::string path_;
    std::fstream file_;
    int num_pages_ = 0;
};

}  // namespace minidb
