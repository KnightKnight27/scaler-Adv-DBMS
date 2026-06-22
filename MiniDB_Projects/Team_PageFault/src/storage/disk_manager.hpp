#pragma once
#include <fstream>
#include <string>
#include "../common/config.hpp"
#include "../common/types.hpp"

namespace minidb {

// The only component that touches the data file. It reads and writes whole
// PAGE_SIZE pages addressed by PageId, and grows the file one page at a time.
class DiskManager {
public:
    explicit DiskManager(const std::string& path);
    ~DiskManager();

    // Read/write a full page. The buffer must be at least PAGE_SIZE bytes.
    void readPage(PageId id, char* dst);
    void writePage(PageId id, const char* src);

    // Grow the file by one zero-filled page and return its id.
    PageId allocatePage();

    PageId numPages() const { return num_pages_; }

private:
    std::string path_;
    std::fstream file_;
    PageId num_pages_ = 0;
};

} // namespace minidb
