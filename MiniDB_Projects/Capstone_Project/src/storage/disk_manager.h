#pragma once

#include "storage/page.h"
#include "common/types.h"
#include <string>

/**
 * @class DiskManager
 * @brief Manages the physical persistence of database pages on disk.
 *
 * Handles low-level raw page I/O using POSIX system calls (open, read, write, lseek, fsync).
 * The database file structure is modeled as a sequence of fixed-size blocks (PAGE_SIZE):
 * - Page 0: Global database metadata (e.g. total number of pages allocated).
 * - Page 1+: Actual data pages.
 */
class DiskManager {
public:
    /**
     * @brief Construct a new Disk Manager.
     */
    DiskManager() = default;

    /**
     * @brief Destructor. Closes the file descriptor if open.
     */
    ~DiskManager();

    // Disable copying for safety (DiskManager owns file handles)
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    /**
     * @brief Opens an existing database file.
     * @param path File system path to the database.
     * @return True on success, false if the file cannot be opened.
     */
    bool open(const std::string& path);

    /**
     * @brief Creates a new database file, initializing page 0 with metadata.
     * @param path File system path to create the database.
     * @return True on success, false on failure.
     */
    bool create(const std::string& path);

    /**
     * @brief Closes the database file, ensuring metadata and cached blocks are flushed.
     */
    void close();

    /**
     * @brief Reads a page from disk into the memory buffer.
     * @param page_id The PageID to read.
     * @param page Target Page struct where data is loaded.
     * @return True if read succeeds, false otherwise.
     */
    bool readPage(PageID page_id, Page& page);

    /**
     * @brief Writes a Page from memory to its corresponding location on disk.
     * @param page_id The PageID to write.
     * @param page Source Page containing the data.
     * @return True if write succeeds, false otherwise.
     */
    bool writePage(PageID page_id, const Page& page);

    /**
     * @brief Allocates a new page at the end of the database file.
     * @return The newly allocated PageID, or INVALID_PAGE_ID on failure.
     */
    PageID allocatePage();

    /**
     * @brief Returns the total page count of the file, including page 0.
     */
    PageID pageCount() const { return num_pages_; }

    /**
     * @brief Forces the operating system to flush buffered writes to the physical device.
     */
    void sync();

    /**
     * @brief Checks if there is an active database file open.
     */
    bool isOpen() const { return fd_ >= 0; }

private:
    /**
     * @brief Computes the byte offset in the file corresponding to a given PageID.
     * @param page_id Page ID to seek.
     */
    off_t pageOffset(PageID page_id) const {
        return static_cast<off_t>(page_id) * static_cast<off_t>(PAGE_SIZE);
    }

    /**
     * @brief Persists the number of pages allocated into the metadata page (page 0).
     */
    void writeMetadata();

    /**
     * @brief Reads the persisted page count metadata from page 0.
     */
    void readMetadata();

    int fd_ = -1;             ///< POSIX file descriptor for database I/O
    PageID num_pages_ = 0;   ///< Persistent page counter (cached in memory)
};
