#include "page_manager.h"
#include <stdexcept>
#include <cstring>

PageManager::PageManager(const std::string& filename) {
    // Try to open an existing file first; if not found, create it.
    file = fopen(filename.c_str(), "r+b");
    if (!file) {
        file = fopen(filename.c_str(), "w+b");
    }
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    // Count how many full pages already exist in the file.
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    num_pages = (int)(file_size / PAGE_SIZE);
}

PageManager::~PageManager() {
    if (file) fclose(file);
}

int PageManager::allocatePage() {
    // Create a zeroed-out page and append it to the file.
    Page blank;
    memset(blank.data, 0, PAGE_SIZE);
    blank.header().page_id  = num_pages;
    blank.header().num_rows = 0;

    fseek(file, (long)num_pages * PAGE_SIZE, SEEK_SET);
    fwrite(blank.data, PAGE_SIZE, 1, file);
    fflush(file);

    return num_pages++;
}

void PageManager::readPage(int page_id, Page& out) {
    fseek(file, (long)page_id * PAGE_SIZE, SEEK_SET);
    size_t n = fread(out.data, PAGE_SIZE, 1, file);
    if (n != 1) {
        throw std::runtime_error("Failed to read page " + std::to_string(page_id));
    }
}

void PageManager::writePage(int page_id, const Page& page) {
    fseek(file, (long)page_id * PAGE_SIZE, SEEK_SET);
    fwrite(page.data, PAGE_SIZE, 1, file);
    fflush(file);
}
