#pragma once

#include <cstdint>
#include <fstream>
#include <string>

static constexpr size_t PAGE_SIZE = 4096; // 4 KB
using page_id_t = int32_t;

static constexpr page_id_t INVALID_PAGE_ID = -1;

class DiskManager {
public:
    explicit DiskManager(const std::string &db_file);
    ~DiskManager();

    void WritePage(page_id_t page_id, const char *page_data);
    void ReadPage(page_id_t page_id, char *page_data);

    page_id_t AllocatePage();
    int32_t GetNumPages() const { return num_pages_; }

private:
    std::fstream db_io_;
    std::string file_name_;
    int32_t num_pages_{0};

    int64_t PageOffset(page_id_t page_id) const {
        return static_cast<int64_t>(page_id) * PAGE_SIZE;
    }
};
