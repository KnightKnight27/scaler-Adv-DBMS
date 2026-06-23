#include "storage/disk_manager.hpp"

#include <filesystem>

namespace minidb {

DiskManager::DiskManager(const std::string& db_file) : db_file_(db_file) {
    namespace fs = std::filesystem;
    if (!fs::exists(db_file_)) {
        std::ofstream create(db_file_, std::ios::binary);  // touch
    }
    file_.open(db_file_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        throw std::runtime_error("DiskManager: cannot open " + db_file_);
    }
    file_.seekg(0, std::ios::end);
    std::streamoff size = file_.tellg();
    num_pages_ = static_cast<PageId>(size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

PageId DiskManager::allocate_page() {
    std::lock_guard<std::mutex> lk(mtx_);
    PageId pid = num_pages_;
    // Extend the file by writing a *valid empty page* (not raw zeros) so that
    // even if its in-memory header is never flushed (e.g. a crash before
    // checkpoint), the on-disk page still reads back as an empty heap page with
    // next_page_id == -1, which terminates a chain scan instead of looping.
    Page empty(pid, /*init=*/true);
    file_.seekp(static_cast<std::streamoff>(pid) * PAGE_SIZE, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(empty.raw()), PAGE_SIZE);
    file_.flush();
    num_pages_++;
    return pid;
}

void DiskManager::read_page(PageId pid, uint8_t* dst) {
    std::lock_guard<std::mutex> lk(mtx_);
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(pid) * PAGE_SIZE, std::ios::beg);
    file_.read(reinterpret_cast<char*>(dst), PAGE_SIZE);
    std::streamsize got = file_.gcount();
    if (got < static_cast<std::streamsize>(PAGE_SIZE)) {
        // Short read (freshly allocated tail) -> zero-fill the remainder.
        std::memset(dst + got, 0, PAGE_SIZE - got);
        file_.clear();
    }
}

void DiskManager::write_page(PageId pid, const uint8_t* src) {
    std::lock_guard<std::mutex> lk(mtx_);
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(pid) * PAGE_SIZE, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(src), PAGE_SIZE);
    file_.flush();
}

void DiskManager::sync() {
    std::lock_guard<std::mutex> lk(mtx_);
    file_.flush();
}

}  // namespace minidb
