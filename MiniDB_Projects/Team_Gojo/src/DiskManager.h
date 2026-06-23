#ifndef MINIDB_DISK_MANAGER_H
#define MINIDB_DISK_MANAGER_H

#include <cstdint>
#include <fstream>
#include <string>

#include "Page.h"

/**
 * DiskManager handles reading and writing fixed-size pages to a database
 * file on disk. Each page is PAGE_SIZE bytes, and pages are addressed by
 * a zero-based pageId that maps directly to a byte offset (pageId * PAGE_SIZE).
 *
 * This class is NOT thread-safe. External synchronization is required
 * if instances are shared across threads.
 */
class DiskManager {
public:
    explicit DiskManager(const std::string& filePath);
    ~DiskManager();

    // Non-copyable, non-movable
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    /**
     * Reads the page identified by pageId into dest.
     * If the page lies beyond the current end of file, dest is zero-filled.
     */
    void readPage(int pageId, uint8_t* dest);

    /**
     * Writes PAGE_SIZE bytes from src to the position identified by pageId.
     */
    void writePage(int pageId, const uint8_t* src);

    /** Returns the total number of pages currently stored in the file. */
    int getNumPages();

    /** Allocates a new page by extending the file with an empty page. */
    int allocatePage();

    /** Flushes any buffered data to the underlying storage device. */
    void sync();

    const std::string& getFilePath() const { return filePath_; }

private:
    void validatePageId(int pageId);

    std::string filePath_;
    std::fstream dbFile_;
};

#endif // MINIDB_DISK_MANAGER_H
