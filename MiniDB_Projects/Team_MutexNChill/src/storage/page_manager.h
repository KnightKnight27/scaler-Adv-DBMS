#pragma once
#include "page.h"
#include <string>
#include <cstdio>

// PageManager handles all disk I/O for one table's data file.
// Each table has its own binary file (e.g. "students.db").
// Pages are stored back-to-back: page 0 at byte 0, page 1 at byte 4096, etc.
class PageManager {
public:
    explicit PageManager(const std::string& filename);
    ~PageManager();

    // Append a new blank page at the end of the file. Returns the new page_id.
    int  allocatePage();

    // Read the page with this id from disk into 'out'.
    void readPage(int page_id, Page& out);

    // Write 'page' to disk at the position for page_id.
    void writePage(int page_id, const Page& page);

    int pageCount() const { return num_pages; }

private:
    FILE* file;
    int   num_pages;
};
