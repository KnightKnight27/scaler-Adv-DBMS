#pragma once

#include <fstream>
#include <string>

#include "../common/types.hpp"

// only thing that touches the data file; reads/writes whole pages by id
class DiskManager {
public:
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    // unwritten page reads back zero-filled
    void read_page(PageID id, char* dest);

    void write_page(PageID id, const char* src);

    // append a zero-filled page, return its id
    PageID allocate_page();

    PageID num_pages() const { return num_pages_; }

private:
    std::string  file_name_;
    std::fstream io_;
    PageID       num_pages_ = 0;
};
