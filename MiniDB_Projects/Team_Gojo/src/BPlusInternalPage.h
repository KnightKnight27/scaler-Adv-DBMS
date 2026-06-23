#ifndef MINIDB_BPLUS_INTERNAL_PAGE_H
#define MINIDB_BPLUS_INTERNAL_PAGE_H

#include <cstring>
#include <stdexcept>

#include "BPlusNode.h"

/**
 * Internal (non-leaf) page of the B+ tree.
 *
 * Layout after the 9-byte header:
 *   child[0] | key[0] | child[1] | key[1] | child[2] | ...
 *
 * Each slot is 8 bytes (4-byte childId + 4-byte key), except the very
 * first child which occupies 4 bytes on its own before the first key.
 *
 * Offsets:
 *   child[i] → HEADER_SIZE + i * 8
 *   key[i]   → HEADER_SIZE + i * 8 + 4
 */
class BPlusInternalPage : public BPlusNode {
public:
    static constexpr int SLOT_SIZE = 8;  // 4-byte child + 4-byte key
    // N keys require N+1 children → total bytes = 4 + N*8
    static constexpr int MAX_KEYS =
        (Page::PAGE_SIZE - HEADER_SIZE - 4) / SLOT_SIZE;  // 510

    explicit BPlusInternalPage(Page* page) : BPlusNode(page) {}

    // ── Accessors ───────────────────────────────────────────────────────

    int32_t getKeyAt(int index) const {
        return readInt(HEADER_SIZE + index * SLOT_SIZE + 4);
    }

    void setKeyAt(int index, int32_t key) {
        writeInt(HEADER_SIZE + index * SLOT_SIZE + 4, key);
        page_->markDirty();
    }

    int32_t getChildIdAt(int index) const {
        return readInt(HEADER_SIZE + index * SLOT_SIZE);
    }

    void setChildIdAt(int index, int32_t childId) {
        writeInt(HEADER_SIZE + index * SLOT_SIZE, childId);
        page_->markDirty();
    }

    // ── Routing ─────────────────────────────────────────────────────────

    /**
     * Returns the child page ID to follow for the given search key.
     *
     * child[0] holds keys < key[0]
     * child[i] holds keys >= key[i-1] and < key[i]
     * child[N] holds keys >= key[N-1]
     */
    int32_t searchChild(int32_t searchKey) const {
        int numKeys = getNumKeys();
        for (int i = 0; i < numKeys; i++) {
            if (searchKey < getKeyAt(i)) {
                return getChildIdAt(i);
            }
        }
        // searchKey >= all keys → rightmost child
        return getChildIdAt(numKeys);
    }

    // ── Sorted insert ───────────────────────────────────────────────────

    /**
     * Inserts a key and its right child pointer into the correct sorted
     * position. Uses memmove() for bulk-shifting bytes.
     * Assumes there is room (numKeys < MAX_KEYS).
     */
    void insertEntry(int32_t key, int32_t rightChildId) {
        int numKeys = getNumKeys();
        if (numKeys >= MAX_KEYS) {
            throw std::runtime_error(
                "Internal page is full. Split required.");
        }

        // Find insertion point (first index whose key > new key)
        int insertIdx = 0;
        while (insertIdx < numKeys && getKeyAt(insertIdx) < key) {
            insertIdx++;
        }

        // Shift keys and children right using memmove
        // We need to shift everything from insertIdx to numKeys-1 right by
        // one slot (8 bytes). This covers both key[i] and child[i+1].
        int slotsToShift = numKeys - insertIdx;
        if (slotsToShift > 0) {
            uint8_t* data = page_->getData();
            // Source: start of key[insertIdx] (which is also child[insertIdx+1] - 4)
            // But we need to shift key[insertIdx]..key[numKeys-1] AND
            // child[insertIdx+1]..child[numKeys]
            // In memory, key[insertIdx] starts at HEADER_SIZE + insertIdx*8 + 4
            // child[numKeys] ends at HEADER_SIZE + numKeys*8 + 4
            int srcOffset = HEADER_SIZE + insertIdx * SLOT_SIZE + 4;
            int dstOffset = srcOffset + SLOT_SIZE;
            int bytesToMove = (HEADER_SIZE + numKeys * SLOT_SIZE + 4) - srcOffset;
            std::memmove(data + dstOffset, data + srcOffset, bytesToMove);
        }

        // Write the new key and right child
        setKeyAt(insertIdx, key);
        setChildIdAt(insertIdx + 1, rightChildId);

        setNumKeys(numKeys + 1);
        page_->markDirty();
    }

    // ── Splitting ───────────────────────────────────────────────────────

    /**
     * Splits this internal page. The right half of keys/children are
     * moved to the new page. The middle key is NOT placed in either
     * page — it gets pushed up to the parent.
     *
     * Returns a BPlusInternalPage wrapping the new blank page.
     */
    BPlusInternalPage split(Page* newBlankPage) {
        BPlusInternalPage newInternal(newBlankPage);
        newInternal.setLeaf(false);
        newInternal.setParentId(this->getParentId());

        int totalKeys = getNumKeys();
        int splitIndex = totalKeys / 2;

        // In internal nodes, the middle key gets PUSHED UP to the parent.
        // It does not stay in either the left or right node.
        int keysToMove = totalKeys - splitIndex - 1;

        uint8_t* srcData = page_->getData();
        uint8_t* dstData = newBlankPage->getData();

        // Copy children and keys for the right half using memcpy
        // Source starts at child[splitIndex+1]: HEADER_SIZE + (splitIndex+1)*8
        // Dest starts at child[0]: HEADER_SIZE
        // Total bytes: keysToMove * SLOT_SIZE + 4 (for the last child)
        int srcOffset = HEADER_SIZE + (splitIndex + 1) * SLOT_SIZE;
        int dstOffset = HEADER_SIZE;
        int bytesToCopy = keysToMove * SLOT_SIZE + 4;
        std::memcpy(dstData + dstOffset, srcData + srcOffset, bytesToCopy);

        newInternal.setNumKeys(keysToMove);
        this->setNumKeys(splitIndex);

        return newInternal;
    }
};

#endif // MINIDB_BPLUS_INTERNAL_PAGE_H
