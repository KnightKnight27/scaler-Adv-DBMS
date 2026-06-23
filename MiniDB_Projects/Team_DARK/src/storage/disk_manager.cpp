#include "storage/disk_manager.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

#ifdef __APPLE__
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace minidb {

namespace {

void ThrowSyscallError(const char* context) {
    throw std::runtime_error(std::string(context) + ": " + std::strerror(errno));
}

}  // namespace

DiskManager::DiskManager(const std::string& db_path) : fd_(-1), db_path_(db_path) {
#ifdef __APPLE__
    fd_ = ::open(db_path.c_str(), O_RDWR | O_CREAT, 0644);
#else
    fd_ = ::open(db_path.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0644);
#endif
    if (fd_ < 0) {
        ThrowSyscallError("open");
    }

#ifdef __APPLE__
    if (::fcntl(fd_, F_NOCACHE, 1) < 0) {
        ThrowSyscallError("fcntl F_NOCACHE");
    }
#endif
}

DiskManager::~DiskManager() {
    Close();
}

void DiskManager::ReadPage(page_id_t page_id, char* page_data) {
    if (page_id < 0) {
        throw std::invalid_argument("page_id must be non-negative");
    }

    const off_t offset = static_cast<off_t>(page_id) * static_cast<off_t>(PAGE_SIZE);
    const ssize_t bytes_read = ::pread(fd_, page_data, PAGE_SIZE, offset);
    if (bytes_read < 0) {
        ThrowSyscallError("pread");
    }
    if (bytes_read < static_cast<ssize_t>(PAGE_SIZE)) {
        std::memset(page_data + bytes_read, 0, PAGE_SIZE - static_cast<std::size_t>(bytes_read));
    }
}

void DiskManager::WritePage(page_id_t page_id, const char* page_data) {
    if (page_id < 0) {
        throw std::invalid_argument("page_id must be non-negative");
    }

    const off_t offset = static_cast<off_t>(page_id) * static_cast<off_t>(PAGE_SIZE);
    const ssize_t bytes_written = ::pwrite(fd_, page_data, PAGE_SIZE, offset);
    if (bytes_written < 0) {
        ThrowSyscallError("pwrite");
    }
    if (bytes_written != static_cast<ssize_t>(PAGE_SIZE)) {
        throw std::runtime_error("pwrite wrote fewer than PAGE_SIZE bytes");
    }
}

page_id_t DiskManager::GetNumPages() const {
    struct stat st {};
    if (::fstat(fd_, &st) != 0) {
        ThrowSyscallError("fstat");
    }
    if (st.st_size <= 0) {
        return 0;
    }
    return static_cast<page_id_t>((st.st_size + static_cast<off_t>(PAGE_SIZE) - 1) /
                                  static_cast<off_t>(PAGE_SIZE));
}

void DiskManager::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

char* DiskManager::AllocatePageBuffer() {
    void* buffer = nullptr;
    if (::posix_memalign(&buffer, PAGE_SIZE, PAGE_SIZE) != 0) {
        throw std::bad_alloc();
    }
    return static_cast<char*>(buffer);
}

void DiskManager::FreePageBuffer(char* buffer) {
    std::free(buffer);
}

}  // namespace minidb
