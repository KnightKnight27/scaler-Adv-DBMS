#include "storage/page_manager.h"
#include <iostream>
#include <stdexcept>

namespace minidb {

PageManager::PageManager(const std::string &db_file) : db_file_(db_file) {
    db_io_.open(db_file_, std::ios::in | std::ios::out | std::ios::binary);
    if (!db_io_.is_open()) {
        db_io_.clear();
        db_io_.open(db_file_, std::ios::out | std::ios::binary);
        db_io_.close();
        db_io_.open(db_file_, std::ios::in | std::ios::out | std::ios::binary);
    }
    
    db_io_.seekg(0, std::ios::end);
    std::streampos size = db_io_.tellg();
    next_page_id_ = size / PAGE_SIZE;
}

PageManager::~PageManager() {
    if (db_io_.is_open()) {
        db_io_.close();
    }
}

void PageManager::ReadPage(page_id_t page_id, Page *page) {
    std::lock_guard<std::mutex> lock(latch_);
    if (page_id >= next_page_id_) {
        throw std::runtime_error("Reading past end of file");
    }
    db_io_.seekg(page_id * PAGE_SIZE, std::ios::beg);
    db_io_.read(page->GetData(), PAGE_SIZE);
}

void PageManager::WritePage(page_id_t page_id, const Page *page) {
    std::lock_guard<std::mutex> lock(latch_);
    if (page_id > next_page_id_) {
        throw std::runtime_error("Writing past allocated pages");
    }
    db_io_.seekp(page_id * PAGE_SIZE, std::ios::beg);
    db_io_.write(page->GetData(), PAGE_SIZE);
    db_io_.flush();
}

page_id_t PageManager::AllocatePage(page_id_t prev_page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    page_id_t new_page_id = next_page_id_++;
    
    Page empty_page;
    empty_page.Init(new_page_id);
    db_io_.seekp(new_page_id * PAGE_SIZE, std::ios::beg);
    db_io_.write(empty_page.GetData(), PAGE_SIZE);
    
    if (prev_page_id != INVALID_PAGE_ID) {
        Page prev_page;
        db_io_.seekg(prev_page_id * PAGE_SIZE, std::ios::beg);
        db_io_.read(prev_page.GetData(), PAGE_SIZE);
        prev_page.SetNextPageId(new_page_id);
        db_io_.seekp(prev_page_id * PAGE_SIZE, std::ios::beg);
        db_io_.write(prev_page.GetData(), PAGE_SIZE);
    }
    
    db_io_.flush();
    
    return new_page_id;
}

void PageManager::Flush() {
    std::lock_guard<std::mutex> lock(latch_);
    db_io_.flush();
}

} // namespace minidb
