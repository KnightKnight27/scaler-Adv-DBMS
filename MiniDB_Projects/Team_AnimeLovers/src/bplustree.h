#pragma once
#include "value.h"
#include <array>
#include <memory>
#include <optional>
#include <vector>

// ─── B+ Tree (in-memory, order 4) ─────────────────────────────────────────
//
// Used as the primary key index.  Maps a Value key to a RID so the query
// executor can do O(log n) point lookups instead of full heap scans.
//
// Order = 4 means:
//   Internal node: up to 3 separator keys, up to 4 children
//   Leaf node    : up to 3 (key, RID) pairs; linked to next leaf for scans
//
// We keep the implementation in-memory for clarity.  On disk a real database
// would serialise these nodes to pages managed by the buffer pool.
// ─────────────────────────────────────────────────────────────────────────────

constexpr int ORDER = 4; // fan-out; leaf/internal capacity = ORDER-1 keys

struct BPTNode {
    bool is_leaf = true;

    // Keys stored in this node (up to ORDER-1)
    std::vector<Value> keys;

    // Leaf: RID values parallel to keys
    std::vector<RID> rids;

    // Internal: child pointers (one more than keys)
    std::vector<BPTNode*> children;

    // Leaf-level linked list for efficient range scans
    BPTNode* next = nullptr;

    BPTNode() = default;
    ~BPTNode() {
        // Only delete children if this is an internal node — leaf rids are POD
        if (!is_leaf)
            for (auto* c : children) delete c;
    }
    // Non-copyable; ownership is through raw pointers for simplicity
    BPTNode(const BPTNode&)            = delete;
    BPTNode& operator=(const BPTNode&) = delete;
};

// ─── BPlusTree ────────────────────────────────────────────────────────────────
class BPlusTree {
public:
    BPlusTree() = default;
    ~BPlusTree() { delete root_; }

    // Insert a (key, rid) pair.  Duplicate keys are rejected (primary key
    // semantics) — returns false if the key already exists.
    bool insert(const Value& key, RID rid);

    // Point lookup: returns the RID if key is found, else nullopt
    std::optional<RID> search(const Value& key) const;

    // Delete the entry for key; returns false if not found
    bool remove(const Value& key);

    // Range scan: collect all RIDs where key ∈ [lo, hi] (inclusive bounds)
    std::vector<RID> range_scan(const Value& lo, const Value& hi) const;

    // Full scan in key order — used by the executor for index-order scans
    std::vector<std::pair<Value, RID>> scan_all() const;

    bool empty() const { return root_ == nullptr; }

private:
    BPTNode* root_ = nullptr;

    // Returns the leaf that should contain key (creates root if empty)
    BPTNode* find_leaf(const Value& key) const;

    // Split helpers — return the new right-sibling node and the key pushed up
    struct SplitResult { BPTNode* right; Value push_up_key; };
    SplitResult split_leaf(BPTNode* leaf);
    SplitResult split_internal(BPTNode* node);

    // Recursive insert; returns non-null SplitResult if the node was split
    std::optional<SplitResult> insert_recursive(BPTNode* node, const Value& key, RID rid);

    // Recursive delete; returns true if entry was removed
    bool remove_from_leaf(BPTNode* leaf, const Value& key);
};
