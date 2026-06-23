#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// bplus_tree.h  –  in-memory B+ tree (int32 key → page_id_t value)
// ─────────────────────────────────────────────────────────────────────────────
#include "common/config.h"
#include "common/types.h"
#include <vector>
#include <cstdint>

namespace minidb {

// Internal node structure.
struct BPNode {
    bool                 is_leaf;
    std::vector<int32_t> keys;
    std::vector<BPNode*> children; // internal: size == keys.size()+1
    std::vector<page_id_t> values; // leaf:     parallel to keys
    BPNode*              next = nullptr; // leaf-level linked list

    explicit BPNode(bool leaf) : is_leaf(leaf) {}
};

/**
 * BPlusTree – supports Insert, Search, Delete.
 * Order = BTREE_ORDER (from config.h).
 * All data lives in leaf nodes; internal nodes are separators only.
 */
class BPlusTree {
public:
    BPlusTree();
    ~BPlusTree();

    void Insert(int32_t key, page_id_t value);   // duplicate key is a no-op
    bool Search(int32_t key, page_id_t* out) const;
    void Delete(int32_t key);                    // lazy remove from leaf
    std::vector<std::pair<int32_t, page_id_t>> ScanRange(int32_t key_low) const;

private:
    BPNode* root_;

    BPNode* FindLeaf(int32_t key) const;
    // Recursive insert; returns sibling if current node split, else nullptr.
    BPNode* InsertRec(BPNode* node, int32_t key, page_id_t val, int32_t* up_key);
    BPNode* SplitLeaf    (BPNode* leaf, int32_t* up_key);
    BPNode* SplitInternal(BPNode* node, int32_t* up_key);
    void    FreeTree(BPNode* node);
};

} // namespace minidb
