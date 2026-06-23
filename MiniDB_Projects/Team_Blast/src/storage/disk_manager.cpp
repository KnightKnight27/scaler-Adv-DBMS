#include "storage/disk_manager.h"

// POSIX headers — same as Lab 1
#include <fcntl.h>     // open(), O_RDWR, O_CREAT, O_TRUNC
#include <unistd.h>    // read(), write(), close(), lseek(), fsync()
#include <sys/stat.h>  // file permissions
#include <cstring>     // memset
#include <iostream>
#include <stdexcept>

// ─── Constructor / Destructor ─────────────────────────────────────────────────

DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        close();
    }
}

// ─── open ─────────────────────────────────────────────────────────────────────

bool DiskManager::open(const std::string& path) {
    // Open existing file for reading and writing (no create, no truncate)
    fd_ = ::open(path.c_str(), O_RDWR, 0644);
    if (fd_ < 0) {
        return false;
    }

    // Read num_pages_ from the metadata stored in page 0
    readMetadata();
    return true;
}

// ─── create ───────────────────────────────────────────────────────────────────

bool DiskManager::create(const std::string& path) {
    // Create a new file (or overwrite existing), open for read+write
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) {
        return false;
    }

    // Initialize: write page 0 as the metadata page.
    // num_pages_ starts at 1 because page 0 itself exists.
    num_pages_ = 1;
    Page meta_page;
    meta_page.init(0);
    // Store num_pages_ in the first 4 bytes of the body
    std::memcpy(meta_page.body, &num_pages_, sizeof(num_pages_));

    if (!writePage(0, meta_page)) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    return true;
}

// ─── close ────────────────────────────────────────────────────────────────────

void DiskManager::close() {
    if (fd_ >= 0) {
        // Persist metadata before closing
        writeMetadata();
        ::fsync(fd_);
        ::close(fd_);
        fd_        = -1;
        num_pages_ = 0;
    }
}

// ─── readPage ─────────────────────────────────────────────────────────────────

bool DiskManager::readPage(PageID page_id, Page& page) {
    if (fd_ < 0 || page_id >= num_pages_) {
        return false;
    }

    // Seek to the correct byte offset in the file
    off_t offset = pageOffset(page_id);
    if (::lseek(fd_, offset, SEEK_SET) < 0) {
        return false;
    }

    // Read exactly PAGE_SIZE bytes into the page buffer
    ssize_t bytes_read = ::read(fd_, &page, PAGE_SIZE);
    return bytes_read == static_cast<ssize_t>(PAGE_SIZE);
}

// ─── writePage ────────────────────────────────────────────────────────────────

bool DiskManager::writePage(PageID page_id, const Page& page) {
    if (fd_ < 0) {
        return false;
    }

    // Seek to the correct byte offset
    off_t offset = pageOffset(page_id);
    if (::lseek(fd_, offset, SEEK_SET) < 0) {
        return false;
    }

    // Write exactly PAGE_SIZE bytes from the page buffer
    ssize_t bytes_written = ::write(fd_, &page, PAGE_SIZE);
    return bytes_written == static_cast<ssize_t>(PAGE_SIZE);
}

// ─── allocatePage ─────────────────────────────────────────────────────────────

PageID DiskManager::allocatePage() {
    PageID new_id = num_pages_;
    num_pages_++;

    // Initialize the new page and write it to disk to physically extend the file
    Page new_page;
    new_page.init(new_id);

    if (!writePage(new_id, new_page)) {
        // Roll back the counter if we couldn't write
        num_pages_--;
        return INVALID_PAGE_ID;
    }

    // Update the metadata page immediately so the allocation survives a crash
    writeMetadata();
    return new_id;
}

// ─── sync ─────────────────────────────────────────────────────────────────────

void DiskManager::sync() {
    if (fd_ >= 0) {
        ::fsync(fd_);
    }
}

// ─── writeMetadata ────────────────────────────────────────────────────────────
// Writes num_pages_ into the body of page 0 so it persists across restarts.

void DiskManager::writeMetadata() {
    Page meta_page;
    meta_page.init(0);
    std::memcpy(meta_page.body, &num_pages_, sizeof(num_pages_));
    writePage(0, meta_page);
}

// ─── readMetadata ─────────────────────────────────────────────────────────────
// Reads num_pages_ back from page 0 after opening an existing file.

void DiskManager::readMetadata() {
    Page meta_page;
    if (readPage(0, meta_page)) {
        std::memcpy(&num_pages_, meta_page.body, sizeof(num_pages_));
    } else {
        // Fallback: file was empty or corrupt — treat as 1 page
        num_pages_ = 1;
    }
}
