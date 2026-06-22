#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include "../common/types.hpp"

namespace minidb {

// An in-memory B+ tree mapping an integer key (the primary key) to a RowId.
//
// Internal nodes store only separator keys and child pointers (they route);
// every RowId lives in a leaf, and leaves are singly linked left-to-right so a
// range scan can walk them directly. The tree is rebuilt from the heap when a
// table is opened (node persistence to disk pages is intentionally out of
// scope for this project).
class BPlusTree {
public:
    // ORDER = max children per internal node; a node splits when it overflows.
    static constexpr int ORDER = 4;

    BPlusTree() = default;
    ~BPlusTree() { destroy(root_); }

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    void insert(int64_t key, RowId rid);
    bool search(int64_t key, RowId& out) const;
    void erase(int64_t key);

    // Visit (key, RowId) for every key in [lo, hi], in key order.
    void rangeScan(int64_t lo, int64_t hi,
                   const std::function<void(int64_t, RowId)>& fn) const;

    size_t size() const { return count_; }

private:
    struct Node {
        bool leaf;
        std::vector<int64_t> keys;
        std::vector<RowId> vals;      // leaf only, parallel to keys
        std::vector<Node*> children;  // internal only, size = keys.size()+1
        Node* next = nullptr;         // leaf chain
        explicit Node(bool l) : leaf(l) {}
    };

    Node* root_ = nullptr;
    size_t count_ = 0;

    Node* findLeaf(int64_t key) const;
    // Insert into subtree; if the child split, returns the pushed-up separator
    // and the new right sibling via out params (split == true).
    void insertInto(Node* node, int64_t key, RowId rid,
                    bool& split, int64_t& up_key, Node*& new_right);
    static void destroy(Node* n);
};

} // namespace minidb
