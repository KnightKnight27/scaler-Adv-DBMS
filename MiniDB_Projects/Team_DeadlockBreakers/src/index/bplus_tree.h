#pragma once

#include "common/types.h"
#include <vector>
#include <optional>
#include <memory>

namespace minidb {

/*
 * BPlusTree: in-memory B+ tree index on int64_t keys → RecordId values.
 * Used for primary key indexing.
 *
 * Design:
 *  - Internal nodes store keys and child pointers.
 *  - Leaf nodes store keys, RecordIds, and a sibling pointer.
 *  - Order (max keys per node) is configurable.
 *  - No concurrent latch-crabbing; assume table-level locking.
 */

class BPlusTree {
public:
    explicit BPlusTree(int order = 64);
    ~BPlusTree();

    // Insert a key-value pair
    void Insert(int64_t key, const RecordId& rid);

    // Delete a key
    bool Delete(int64_t key);

    // Search for a key, returns RecordId if found
    std::optional<RecordId> Search(int64_t key) const;

    // Range search [lo, hi] inclusive
    std::vector<RecordId> RangeSearch(int64_t lo, int64_t hi) const;

    // Check if the tree is empty
    bool IsEmpty() const { return root_ == nullptr; }

    // Number of entries
    size_t Size() const { return size_; }

private:
    struct Node {
        bool is_leaf;
        std::vector<int64_t> keys;
        virtual ~Node() = default;
    protected:
        Node(bool leaf) : is_leaf(leaf) {}
    };

    struct InternalNode : Node {
        std::vector<Node*> children;
        InternalNode() : Node(false) {}
    };

    struct LeafNode : Node {
        std::vector<RecordId> values;
        LeafNode* next = nullptr; // sibling pointer for range scans
        LeafNode() : Node(true) {}
    };

    int    order_;    // max keys per node
    Node*  root_ = nullptr;
    size_t size_ = 0;

    // Find the leaf node where key should be
    LeafNode* FindLeaf(int64_t key) const;

    // Insert into a leaf, possibly splitting upward
    void InsertIntoLeaf(LeafNode* leaf, int64_t key, const RecordId& rid);

    // Split a leaf node, returns the new key to push up and the new node
    void SplitLeaf(LeafNode* leaf, int64_t& push_key, LeafNode*& new_leaf);

    // Insert a key into parent, splitting if needed
    void InsertIntoParent(Node* left, int64_t key, Node* right);

    // Find parent of a node (simple search from root)
    InternalNode* FindParent(Node* target) const;
    InternalNode* FindParentHelper(InternalNode* current, Node* target) const;

    // Split an internal node
    void SplitInternal(InternalNode* node, int64_t& push_key, InternalNode*& new_node);

    // Delete helpers
    bool DeleteFromLeaf(LeafNode* leaf, int64_t key);

    // Cleanup
    void DestroyTree(Node* node);
};

} // namespace minidb
