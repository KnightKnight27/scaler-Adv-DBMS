#include "heap_file.h"

HeapFile::HeapFile(const std::string& path) : path_(path) {
    file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        std::fstream create(path, std::ios::out | std::ios::binary);
        create.close();
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
}

HeapFile::~HeapFile() {
    if (file_.is_open()) file_.close();
}

Page HeapFile::read_page(uint32_t page_id) {
    Page p;
    p.id = page_id;
    file_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    file_.read(reinterpret_cast<char*>(p.data), PAGE_SIZE);
    return p;
}

void HeapFile::write_page(const Page& page) {
    file_.seekp(static_cast<std::streamoff>(page.id) * PAGE_SIZE);
    file_.write(reinterpret_cast<const char*>(page.data), PAGE_SIZE);
    file_.flush();
}

uint32_t HeapFile::total_pages() {
    file_.seekg(0, std::ios::end);
    auto sz = file_.tellg();
    if (sz <= 0) return 0;
    return static_cast<uint32_t>(sz) / PAGE_SIZE;
}

uint32_t HeapFile::alloc_page() {
    uint32_t id = total_pages();
    uint8_t zeros[PAGE_SIZE]{};
    file_.seekp(static_cast<std::streamoff>(id) * PAGE_SIZE);
    file_.write(reinterpret_cast<const char*>(zeros), PAGE_SIZE);
    file_.flush();
    return id;
}
