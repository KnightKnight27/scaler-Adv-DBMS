#include "storage/disk_manager.h"
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace minidb {

DiskManager::DiskManager(const std::string &db_file, bool truncate) : file_name_(db_file) {
    int flags = O_RDWR | O_CREAT;
    if (truncate) flags |= O_TRUNC;
    fd_ = ::open(file_name_.c_str(), flags, 0644);
    if (fd_ < 0) throw std::runtime_error("DiskManager: cannot open file " + file_name_);

    struct stat st;
    if (::fstat(fd_, &st) != 0) throw std::runtime_error("DiskManager: fstat failed");
    num_pages_ = static_cast<int>(st.st_size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) ::close(fd_);
}

void DiskManager::write_page(page_id_t page_id, const char *page_data) {
    std::lock_guard<std::mutex> guard(latch_);
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    ssize_t n = ::pwrite(fd_, page_data, PAGE_SIZE, offset);
    if (n != (ssize_t)PAGE_SIZE) throw std::runtime_error("DiskManager: short write");
    // NOTE: no fsync per write. The buffer pool defers writes; durability of
    // committed data is provided by the WAL (force-flushed at commit). Per-page
    // fsync here would slow the engine by orders of magnitude.
    ++num_writes_;
}

void DiskManager::read_page(page_id_t page_id, char *page_data) {
    std::lock_guard<std::mutex> guard(latch_);
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    ssize_t n = ::pread(fd_, page_data, PAGE_SIZE, offset);
    if (n < 0) throw std::runtime_error("DiskManager: read failed");
    // A short read (e.g. a freshly allocated page past EOF) is zero-filled so
    // callers always receive a full, well-defined page.
    if (n < (ssize_t)PAGE_SIZE) std::memset(page_data + n, 0, PAGE_SIZE - n);
}

page_id_t DiskManager::allocate_page() {
    std::lock_guard<std::mutex> guard(latch_);
    return num_pages_++;
}

void DiskManager::sync() {
    std::lock_guard<std::mutex> guard(latch_);
    ::fsync(fd_);
}

} // namespace minidb
