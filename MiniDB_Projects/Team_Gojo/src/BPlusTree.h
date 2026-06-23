#ifndef MINIDB_BPLUS_TREE_H
#define MINIDB_BPLUS_TREE_H

#include <cstdint>

#include "BufferPool.h"

/**
 * B+ tree index supporting search and insert with automatic
 * split propagation.
 *
 * Keys and record IDs are 4-byte int32_t values.
 * The tree is backed by a BufferPool for page-level caching.
 */
class BPlusTree {
public:
    /**
     * Opens an existing B+ tree whose root is at the given page.
     */
    BPlusTree(BufferPool* pool, int rootPageId);

    /**
     * Creates a brand-new B+ tree, allocating an empty leaf root page.
     */
    explicit BPlusTree(BufferPool* pool);

    /**
     * Searches for the given key in the B+ tree.
     * Returns the recordId if found, or -1 if not found.
     */
    int search(int32_t searchKey);

    /**
     * Inserts a key-recordId pair into the B+ tree,
     * splitting nodes as needed.
     */
    void insert(int32_t key, int32_t recordId);

    int getRootPageId() const { return rootPageId_; }

private:
    BufferPool* pool_;  // non-owning
    int rootPageId_;
};

#endif // MINIDB_BPLUS_TREE_H
