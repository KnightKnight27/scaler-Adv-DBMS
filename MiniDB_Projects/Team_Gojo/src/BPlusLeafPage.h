#ifndef MINIDB_BPLUS_LEAF_PAGE_H
#define MINIDB_BPLUS_LEAF_PAGE_H

#include <cstring>
#include <stdexcept>

#include "BPlusNode.h"

/**
 * Leaf page of the B+ tree. Stores sorted key-recordId pairs.
 *
 * Layout after the 9-byte header:
 *   key[0] | recordId[0] | key[1] | recordId[1] | ...
 *
 * Each pair is PAIR_SIZE (8) bytes: 4-byte key + 4-byte recordId.
 */
class BPlusLeafPage : public BPlusNode {
public:
    static constexpr int PAIR_SIZE = 8;  // 4-byte key + 4-byte recordId
    static constexpr int MAX_PAIRS =
        (Page::PAGE_SIZE - HEADER_SIZE) / PAIR_SIZE;  // 510

    explicit BPlusLeafPage(Page* page) : BPlusNode(page) {}

    // ── Pair accessors ──────────────────────────────────────────────────

    int32_t getKeyAt(int index) const {
        return readInt(pairOffset(index));
    }

    int32_t getRecordIdAt(int index) const {
        return readInt(pairOffset(index) + 4);
    }

    // ── Sorted insert ───────────────────────────────────────────────────

    /**
     * Inserts a key-recordId pair in sorted order.
     * Uses memmove() for shifting existing pairs right.
     */
    void insertPair(int32_t key, int32_t recordId) {
        int numKeys = getNumKeys();
        if (numKeys >= MAX_PAIRS) {
            throw std::runtime_error(
                "Leaf page is full. Split required.");
        }

        // Find insertion point (first index whose key >= new key)
        int insertIdx = 0;
        while (insertIdx < numKeys && getKeyAt(insertIdx) < key) {
            insertIdx++;
        }

        // Shift pairs [insertIdx .. numKeys-1] right by PAIR_SIZE bytes
        int pairsToMove = numKeys - insertIdx;
        if (pairsToMove > 0) {
            uint8_t* data = page_->getData();
            int srcOffset = pairOffset(insertIdx);
            int dstOffset = srcOffset + PAIR_SIZE;
            std::memmove(data + dstOffset, data + srcOffset,
                         pairsToMove * PAIR_SIZE);
        }

        // Write the new pair at insertIdx
        writeInt(pairOffset(insertIdx), key);
        writeInt(pairOffset(insertIdx) + 4, recordId);

        setNumKeys(numKeys + 1);
        page_->markDirty();
    }

    // ── Splitting ───────────────────────────────────────────────────────

    /**
     * Splits this leaf page. The right half of pairs are copied to
     * the new page using memcpy.
     *
     * Returns a BPlusLeafPage wrapping the new blank page.
     */
    BPlusLeafPage split(Page* newBlankPage) {
        BPlusLeafPage newLeaf(newBlankPage);
        newLeaf.setLeaf(true);
        newLeaf.setParentId(this->getParentId());

        int totalKeys = getNumKeys();
        int splitIndex = totalKeys / 2;
        int keysToMove = totalKeys - splitIndex;

        // Copy the right half of the pairs to the new leaf
        uint8_t* srcData = page_->getData();
        uint8_t* dstData = newBlankPage->getData();

        int srcOffset = pairOffset(splitIndex);
        int dstOffset = pairOffset(0);  // HEADER_SIZE in the new page

        std::memcpy(dstData + dstOffset, srcData + srcOffset,
                    keysToMove * PAIR_SIZE);

        // Update counts
        newLeaf.setNumKeys(keysToMove);
        this->setNumKeys(splitIndex);

        return newLeaf;
    }

private:
    static int pairOffset(int index) {
        return HEADER_SIZE + index * PAIR_SIZE;
    }
};

#endif // MINIDB_BPLUS_LEAF_PAGE_H
