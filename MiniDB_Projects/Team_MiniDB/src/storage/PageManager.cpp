#include "storage/PageManager.hpp"

#include <fstream>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

using namespace std;

namespace minidb {

PageManager::PageManager(BufferPool& buffer_pool) : buffer_pool_(buffer_pool) {}

bool PageManager::open(const string& filepath) {
    filepath_ = filepath;

    // Create the parent directory if one exists in the path
    size_t last_slash = filepath.find_last_of("/\\");
    if (last_slash != string::npos) {
        string parent_dir = filepath.substr(0, last_slash);
#ifdef _WIN32
        _mkdir(parent_dir.c_str());
#else
        mkdir(parent_dir.c_str(), 0777);
#endif
    }

    // Check if file exists and get its size using standard streams
    ifstream check_file(filepath, ios::binary | ios::ate);
    bool exists = check_file.good();
    size_t file_sz = 0;
    if (exists) {
        file_sz = check_file.tellg();
        check_file.close();
    }

    file_.open(filepath, ios::in | ios::out | ios::binary);
    if (!file_.is_open()) {
        file_.open(filepath, ios::out | ios::binary);
        file_.close();
        file_.open(filepath, ios::in | ios::out | ios::binary);
    }

    if (exists) {
        page_count_ = static_cast<uint32_t>(file_sz / PAGE_SIZE);
    } else {
        page_count_ = 0;
    }
    return file_.is_open();
}

void PageManager::close() {
    flushAll();
    if (file_.is_open()) file_.close();
}

void PageManager::extendFile(uint32_t new_page_count) {
    if (new_page_count <= page_count_) return;
    file_.seekp(0, ios::end);
    vector<char> zeros(PAGE_SIZE, 0);
    for (uint32_t i = page_count_; i < new_page_count; ++i) {
        file_.write(zeros.data(), PAGE_SIZE);
    }
    page_count_ = new_page_count;
}

uint32_t PageManager::allocatePage(PageType type) {
    uint32_t id = page_count_;
    extendFile(page_count_ + 1);
    Page page;
    page.init(id, type);
    buffer_pool_.putPage(id, page, true);
    return id;
}

Page* PageManager::fetchPage(uint32_t page_id) {
    if (page_id >= page_count_) return nullptr;

    Page* cached = buffer_pool_.getPage(page_id);
    if (cached != nullptr) return cached;

    file_.seekg(static_cast<streamoff>(page_id) * PAGE_SIZE);
    Page page;
    file_.read(page.getData(), PAGE_SIZE);
    buffer_pool_.putPage(page_id, page, false);
    return buffer_pool_.getPage(page_id);
}

void PageManager::markDirty(uint32_t page_id) { buffer_pool_.markDirty(page_id); }

void PageManager::flushAll() {
    auto dirty = buffer_pool_.getDirtyPages();
    for (auto& entry : dirty) {
        file_.seekp(static_cast<streamoff>(entry.first) * PAGE_SIZE);
        file_.write(entry.second.getData(), PAGE_SIZE);
        buffer_pool_.markClean(entry.first);
    }
    file_.flush();
}

}  // namespace minidb
