#pragma once

#include "common/types.h"
#include "common/config.h"
#include <vector>
#include <optional>
#include <functional>

// ─── B+ Tree ──────────────────────────────────────────────────────────────────
//
// A B+ tree index over integer keys (int32_t).
// Values stored at the leaves are RecordIDs (page_id + slot_id).
//
// Design:
//   - ORDER = BTREE_ORDER (from config.h, default 4)
//   - Internal nodes hold up to ORDER-1 keys and ORDER child pointers
//   - Leaf nodes hold up to ORDER-1 key-RecordID pairs
//   - Leaves are linked via next_leaf for range scans
//   - Entirely in-memory for simplicity (nodes are heap-allocated structs)
//   - Makes splits and merges easy to demonstrate and explain in viva
//
// This satisfies the requirement for a primary key index with search/insert/delete.

class BPlusTree {
public:
    BPlusTree() = default;
    ~BPlusTree();

    // Insert a key-value pair.
    // If the key already exists, the old RecordID is overwritten.
    void insert(int32_t key, RecordID rid);

    // Search for a key.
    // Returns the RecordID if found, nullopt if the key doesn't exist.
    std::optional<RecordID> search(int32_t key) const;

    // Delete a key from the tree.
    // Returns true if the key was found and removed, false otherwise.
    bool remove(int32_t key);

    // Iterate over all entries in sorted key order.
    // Calls callback(key, rid) for each leaf entry.
    void scanAll(std::function<void(int32_t, const RecordID&)> callback) const;

    // Print the tree level by level for debugging / viva demo.
    void printTree() const;

    // Number of entries in the tree.
    size_t size() const { return num_entries_; }

private:
    // ── Node structures ──────────────────────────────────────────────────────

    // Both internal and leaf nodes use this struct.
    // is_leaf controls which union members are valid.
    struct Node {
        bool     is_leaf    = true;
        int      num_keys   = 0;
        int32_t  keys[BTREE_ORDER];         // keys[0..num_keys-1]

        // Internal node children: children[0..num_keys] (num_keys+1 pointers)
        Node*    children[BTREE_ORDER + 1];

        // Leaf node values: values[0..num_keys-1]
        RecordID values[BTREE_ORDER];

        // Leaf linked-list pointer
        Node*    next_leaf  = nullptr;

        Node() {
            std::fill(children, children + BTREE_ORDER + 1, nullptr);
        }
    };

    // ── Internal helpers ─────────────────────────────────────────────────────

    // Recursively insert into subtree rooted at node.
    // If the node splits, set promoted_key and new_right_child.
    void insertInto(Node* node, int32_t key, RecordID rid,
                    int32_t& promoted_key, Node*& new_right_child);

    // Split a leaf node. Returns the right half as a new node.
    // promoted_key is the first key of the right leaf (copied up).
    Node* splitLeaf(Node* leaf, int32_t& promoted_key);

    // Split an internal node. Returns the right half as a new node.
    // promoted_key is the middle key (pushed up).
    Node* splitInternal(Node* node, int32_t& promoted_key);

    // Recursively search for a key starting from node.
    std::optional<RecordID> searchFrom(const Node* node, int32_t key) const;

    // Find the leaf node where key lives (or would be inserted).
    Node* findLeaf(int32_t key) const;

    // Delete key from subtree. Returns true if the key was found.
    // Under-flow merging/redistribution is simplified: we do a lazy approach
    // that leaves slightly under-full nodes rather than full merge cascades.
    bool removeFrom(Node* node, Node* parent, int child_idx, int32_t key);

    // Free all nodes recursively.
    void freeTree(Node* node);

    Node*  root_        = nullptr;
    size_t num_entries_ = 0;
};
