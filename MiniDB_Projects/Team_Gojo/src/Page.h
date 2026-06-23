#ifndef MINIDB_PAGE_H
#define MINIDB_PAGE_H

#include <cstdint>
#include <cstring>

/**
 * Represents a single fixed-size page in the database.
 *
 * A page holds PAGE_SIZE bytes of data and tracks whether it has been
 * modified (dirty) since it was last read from or written to disk.
 */
class Page {
public:
    static constexpr int PAGE_SIZE = 4096;

    /**
     * Creates a new blank (zero-filled) page with the given id.
     */
    explicit Page(int pageId)
        : pageId_(pageId), dirty_(false)
    {
        std::memset(data_, 0, PAGE_SIZE);
    }

    /**
     * Creates a page with the given id and a copy of the supplied data.
     */
    Page(int pageId, const uint8_t* src)
        : pageId_(pageId), dirty_(false)
    {
        std::memcpy(data_, src, PAGE_SIZE);
    }

    int getPageId() const { return pageId_; }

    /**
     * Returns a direct pointer to the internal data array.
     * Callers that modify the array should call markDirty() afterwards.
     */
    uint8_t* getData() { return data_; }
    const uint8_t* getData() const { return data_; }

    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }

    /** Clears the dirty flag, typically called after flushing to disk. */
    void clearDirty() { dirty_ = false; }

private:
    int pageId_;
    uint8_t data_[PAGE_SIZE];
    bool dirty_;
};

#endif // MINIDB_PAGE_H
