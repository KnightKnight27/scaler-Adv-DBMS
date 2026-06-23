#include "storage/disk_manager.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <array>
#include <cstring>

#include "common/exception.h"

namespace minidb {

namespace {

// Standard CRC32 (reflected, polynomial 0xEDB88820). Bitwise version is plenty
// fast for one 4 KiB page and keeps the code dependency-free.
std::uint32_t crc32(const char* data, std::size_t len) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<std::uint8_t>(data[i]);
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88820u & (~((crc & 1u) - 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

// Checksum covers everything except the 4-byte checksum field itself.
std::uint32_t page_checksum(const char* page) {
    return crc32(page + 4, PAGE_SIZE - 4);
}

} // namespace

DiskManager::DiskManager(const std::string& db_file)
    : file_name_(db_file), fd_(-1), num_pages_(0) {
    fd_ = ::open(db_file.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) throw DBException("DiskManager: cannot open file " + db_file);

    struct stat st{};
    if (::fstat(fd_, &st) != 0) throw DBException("DiskManager: fstat failed for " + db_file);
    num_pages_ = static_cast<std::size_t>(st.st_size) / PAGE_SIZE;
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) ::close(fd_);
}

void DiskManager::read_page(PageId pid, char* out) {
    if (pid < 0 || static_cast<std::size_t>(pid) >= num_pages_)
        throw DBException("DiskManager: read out-of-range page " + std::to_string(pid));

    off_t offset = static_cast<off_t>(pid) * PAGE_SIZE;
    ssize_t n = ::pread(fd_, out, PAGE_SIZE, offset);
    if (n != static_cast<ssize_t>(PAGE_SIZE))
        throw DBException("DiskManager: short read on page " + std::to_string(pid));

    std::uint32_t stored;
    std::memcpy(&stored, out, sizeof(stored));
    if (stored != page_checksum(out))
        throw DBException("DiskManager: checksum mismatch on page " + std::to_string(pid));
}

void DiskManager::write_page(PageId pid, const char* in) {
    if (pid < 0 || static_cast<std::size_t>(pid) >= num_pages_)
        throw DBException("DiskManager: write out-of-range page " + std::to_string(pid));

    // Stamp the checksum into a local copy so we never mutate the caller's page.
    std::array<char, PAGE_SIZE> buf{};
    std::memcpy(buf.data(), in, PAGE_SIZE);
    std::uint32_t crc = page_checksum(buf.data());
    std::memcpy(buf.data(), &crc, sizeof(crc));

    off_t offset = static_cast<off_t>(pid) * PAGE_SIZE;
    ssize_t n = ::pwrite(fd_, buf.data(), PAGE_SIZE, offset);
    if (n != static_cast<ssize_t>(PAGE_SIZE))
        throw DBException("DiskManager: short write on page " + std::to_string(pid));
}

PageId DiskManager::allocate_page() {
    PageId pid = static_cast<PageId>(num_pages_);
    ++num_pages_;
    // Write a zeroed, properly-checksummed page so the new page reads back cleanly.
    std::array<char, PAGE_SIZE> zero{};
    write_page(pid, zero.data());
    return pid;
}

void DiskManager::sync() {
    if (fd_ >= 0) ::fsync(fd_);
}

} // namespace minidb
