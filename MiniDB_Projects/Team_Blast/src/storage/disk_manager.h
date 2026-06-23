#pragma once

#include "storage/page.h"
#include "common/types.h"
#include <string>

// ─── DiskManager ──────────────────────────────────────────────────────────────
//
// Manages the raw database file on disk.
// Provides page-level read and write operations using POSIX system calls:
//   open(), read(), write(), lseek(), close()  (same as Lab 1)
//
// The database file is organized as a sequence of fixed-size pages:
//   Page 0: metadata (num_pages allocated so far)
//   Page 1+: data pages
//
// This class has NO knowledge of what is inside a page — it just moves
// PAGE_SIZE bytes between memory and disk at the right offset.

class DiskManager {
public:
    DiskManager() = default;
    ~DiskManager();

    // Open an existing database file.
    // Returns true on success, false if file does not exist.
    bool open(const std::string& path);

    // Create a new database file (or overwrite an existing one).
    // Returns true on success.
    bool create(const std::string& path);

    // Close the database file.
    void close();

    // Read a page from disk into the provided Page buffer.
    // Returns true on success.
    bool readPage(PageID page_id, Page& page);

    // Write a Page from memory to its slot on disk.
    // Returns true on success.
    bool writePage(PageID page_id, const Page& page);

    // Allocate a new page at the end of the file.
    // Initializes it (zeros it out), writes it to disk, returns the new PageID.
    PageID allocatePage();

    // Total number of pages in the file (including page 0).
    PageID pageCount() const { return num_pages_; }

    // Force OS to flush any buffered writes to storage device.
    void sync();

    bool isOpen() const { return fd_ >= 0; }

private:
    // Compute the byte offset in the file where page_id lives.
    // Page 0 is at offset 0, page 1 at offset PAGE_SIZE, etc.
    off_t pageOffset(PageID page_id) const {
        return static_cast<off_t>(page_id) * static_cast<off_t>(PAGE_SIZE);
    }

    // Write num_pages_ to page 0 (the metadata page) so it persists across restarts.
    void writeMetadata();

    // Read num_pages_ from page 0 after opening an existing file.
    void readMetadata();

    int    fd_        = -1;  // POSIX file descriptor
    PageID num_pages_ = 0;   // total pages allocated (persisted in page 0)
};
