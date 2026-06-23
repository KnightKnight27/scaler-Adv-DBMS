#pragma once
#include "page.h"
#include <string>
#include <fstream>

class HeapFile {
public:
    explicit HeapFile(const std::string& path);
    ~HeapFile();

    Page     read_page(uint32_t page_id);
    void     write_page(const Page& page);
    uint32_t total_pages();
    uint32_t alloc_page();

private:
    std::fstream file_;
    std::string  path_;
};
