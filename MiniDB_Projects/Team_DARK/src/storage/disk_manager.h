#pragma once

#include "storage/page.h"

#include <cstddef>
#include <string>

namespace minidb {

class DiskManager {
public:
    explicit DiskManager(const std::string& db_path);
    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    void ReadPage(page_id_t page_id, char* page_data);
    void WritePage(page_id_t page_id, const char* page_data);
    page_id_t GetNumPages() const;
    void Close();

    static char* AllocatePageBuffer();
    static void FreePageBuffer(char* buffer);

private:
    int fd_;
    std::string db_path_;
};

}  // namespace minidb
