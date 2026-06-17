#pragma once

#include <string>

// ============================================================
// DiskManager — handles raw page I/O to a single database file
//
// Each page is PAGE_SIZE bytes stored at offset (page_id * PAGE_SIZE).
// Uses POSIX file descriptors (open/read/write/lseek/close)
// just like we learned in Lab01.
// ============================================================

class DiskManager {
public:
    // Opens (or creates) the database file at the given path.
    explicit DiskManager(const std::string& file_path);
    ~DiskManager();

    // No copying — the file descriptor is ours alone
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    // Read page_id's worth of PAGE_SIZE bytes into data buffer.
    void ReadPage(int page_id, char* data);

    // Write PAGE_SIZE bytes from data buffer to page_id's location.
    void WritePage(int page_id, const char* data);

    // Allocate a new page at the end of the file.
    // Returns the new page_id.
    int AllocatePage();

    // How many pages currently exist in the file.
    int GetNumPages() const { return num_pages_; }

    // Get the file path
    const std::string& GetFilePath() const { return file_path_; }

private:
    std::string file_path_;
    int fd_;            // POSIX file descriptor
    int num_pages_;     // count of pages currently in the file
};
