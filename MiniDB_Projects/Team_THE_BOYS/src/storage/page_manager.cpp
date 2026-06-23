#include "storage/page_manager.h"

#include <filesystem>
#include <stdexcept>

namespace minidb {

PageManager::PageManager(std::string filepath) : filepath_(std::move(filepath)) {
    namespace fs = std::filesystem;
    bool exists = fs::exists(filepath_);
    file_.open(filepath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        file_.clear();
        file_.open(filepath_, std::ios::out | std::ios::binary | std::ios::trunc);
        file_.close();
        file_.open(filepath_, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open database file: " + filepath_);
    }
    if (exists) {
        file_.seekg(0, std::ios::end);
        page_count_ = static_cast<int>(file_.tellg() / PAGE_SIZE);
    } else {
        page_count_ = 0;
    }
}

PageManager::~PageManager() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

int PageManager::AllocatePage() {
    std::lock_guard<std::mutex> lock(mutex_);
    int page_id = page_count_++;
    file_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    std::vector<char> blank(PAGE_SIZE, '\0');
    file_.write(blank.data(), PAGE_SIZE);
    file_.flush();
    return page_id;
}

Page* PageManager::FetchPage(int page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (page_id < 0 || page_id >= page_count_) {
        throw std::runtime_error("Invalid page id: " + std::to_string(page_id));
    }
    std::vector<char> buffer(PAGE_SIZE);
    file_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    file_.read(buffer.data(), PAGE_SIZE);
    if (!file_) {
        throw std::runtime_error("Failed to read page " + std::to_string(page_id));
    }
    return new Page(page_id, buffer.data());
}

void PageManager::FlushPage(int page_id, const Page& page) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    std::vector<char> buffer(PAGE_SIZE);
    page.WriteBack(buffer.data());
    file_.write(buffer.data(), PAGE_SIZE);
    file_.flush();
}

void PageManager::FlushAll() {
    file_.flush();
}

}  // namespace minidb
