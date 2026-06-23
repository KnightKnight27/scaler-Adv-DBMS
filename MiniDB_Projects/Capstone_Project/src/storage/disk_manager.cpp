#include "storage/disk_manager.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

DiskManager::~DiskManager() {
    if (isOpen()) {
        close();
    }
}

bool DiskManager::open(const std::string& path) {
    // Open the existing database file with read/write access
    fd_ = ::open(path.c_str(), O_RDWR, 0644);
    if (fd_ < 0) {
        return false;
    }

    // Populate the database metadata (number of pages) from page 0
    readMetadata();
    return true;
}

bool DiskManager::create(const std::string& path) {
    // Create new file, or overwrite existing, with read/write permission
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) {
        return false;
    }

    // Allocate the metadata page (page 0)
    num_pages_ = 1;
    Page meta_page;
    meta_page.init(0);
    
    // Copy page count metadata into the start of the page payload
    std::memcpy(meta_page.body, &num_pages_, sizeof(num_pages_));

    if (!writePage(0, meta_page)) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    return true;
}

void DiskManager::close() {
    if (fd_ >= 0) {
        // Guarantee metadata is written and flushed before closing
        writeMetadata();
        sync();
        ::close(fd_);
        fd_ = -1;
        num_pages_ = 0;
    }
}

bool DiskManager::readPage(PageID page_id, Page& page) {
    if (fd_ < 0 || page_id >= num_pages_) {
        return false;
    }

    const off_t offset = pageOffset(page_id);
    if (::lseek(fd_, offset, SEEK_SET) < 0) {
        return false;
    }

    const ssize_t bytes_read = ::read(fd_, &page, PAGE_SIZE);
    return bytes_read == static_cast<ssize_t>(PAGE_SIZE);
}

bool DiskManager::writePage(PageID page_id, const Page& page) {
    if (fd_ < 0) {
        return false;
    }

    const off_t offset = pageOffset(page_id);
    if (::lseek(fd_, offset, SEEK_SET) < 0) {
        return false;
    }

    const ssize_t bytes_written = ::write(fd_, &page, PAGE_SIZE);
    return bytes_written == static_cast<ssize_t>(PAGE_SIZE);
}

PageID DiskManager::allocatePage() {
    const PageID new_id = num_pages_;
    num_pages_++;

    // Format new page structures to extend file layout on disk
    Page new_page;
    new_page.init(new_id);

    if (!writePage(new_id, new_page)) {
        num_pages_--; // Roll back counter upon I/O write failures
        return INVALID_PAGE_ID;
    }

    writeMetadata();
    return new_id;
}

void DiskManager::sync() {
    if (fd_ >= 0) {
        ::fsync(fd_);
    }
}

void DiskManager::writeMetadata() {
    Page meta_page;
    meta_page.init(0);
    std::memcpy(meta_page.body, &num_pages_, sizeof(num_pages_));
    writePage(0, meta_page);
}

void DiskManager::readMetadata() {
    Page meta_page;
    if (readPage(0, meta_page)) {
        std::memcpy(&num_pages_, meta_page.body, sizeof(num_pages_));
    } else {
        num_pages_ = 1; // Fallback default setup if page 0 read fails
    }
}
