#ifndef MINIDB_BPLUS_NODE_H
#define MINIDB_BPLUS_NODE_H

#include <cstdint>
#include <cstring>

#include "Page.h"

/**
 * Base class for B+ tree nodes. Provides access to the common header
 * stored in the first HEADER_SIZE bytes of every node page:
 *
 *   Byte 0       : isLeaf  (1 byte)
 *   Bytes 1–4    : numKeys (4 bytes, int32_t)
 *   Bytes 5–8    : parentId (4 bytes, int32_t)
 *
 * Uses memcpy for reading/writing int32_t to avoid undefined behavior
 * from unaligned casts.
 */
class BPlusNode {
public:
    static constexpr int IS_LEAF_OFFSET   = 0;  // 1 byte
    static constexpr int NUM_KEYS_OFFSET  = 1;  // 4 bytes
    static constexpr int PARENT_ID_OFFSET = 5;  // 4 bytes
    static constexpr int HEADER_SIZE      = 9;

    explicit BPlusNode(Page* page) : page_(page) {}

    bool isLeaf() const {
        return page_->getData()[IS_LEAF_OFFSET] == 1;
    }

    void setLeaf(bool isLeaf) {
        page_->getData()[IS_LEAF_OFFSET] = isLeaf ? 1 : 0;
        page_->markDirty();
    }

    int32_t getNumKeys() const {
        return readInt(NUM_KEYS_OFFSET);
    }

    void setNumKeys(int32_t numKeys) {
        writeInt(NUM_KEYS_OFFSET, numKeys);
        page_->markDirty();
    }

    int32_t getParentId() const {
        return readInt(PARENT_ID_OFFSET);
    }

    void setParentId(int32_t parentId) {
        writeInt(PARENT_ID_OFFSET, parentId);
        page_->markDirty();
    }

    int getPageId() const {
        return page_->getPageId();
    }

protected:
    Page* page_;  // non-owning

    /** Read a 4-byte int32_t from the page at the given byte offset. */
    int32_t readInt(int offset) const {
        int32_t val;
        std::memcpy(&val, page_->getData() + offset, sizeof(int32_t));
        return val;
    }

    /** Write a 4-byte int32_t to the page at the given byte offset. */
    void writeInt(int offset, int32_t val) {
        std::memcpy(page_->getData() + offset, &val, sizeof(int32_t));
    }
};

#endif // MINIDB_BPLUS_NODE_H
