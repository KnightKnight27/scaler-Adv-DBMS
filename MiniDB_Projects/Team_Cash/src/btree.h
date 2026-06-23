// B+ tree primary-key index: maps an integer key to the RID of its row.
//
// Internal nodes hold separator keys and child pointers (routing only). Leaf
// nodes hold the real (key -> RID) entries in sorted order and are chained
// left-to-right with a `next` pointer, so range scans are a linked-list walk.
//
// The tree lives in memory; the Catalog rebuilds it on startup by scanning the
// heap, so it is never persisted itself (documented trade-off).
#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "types.h"

namespace minidb {

class BPlusTree {
public:
    // order = max children per node. A small default (4) means nodes hold at
    // most 3 keys, so splits happen early and are easy to see in tests/demos.
    explicit BPlusTree(int order = 4) : order_(order), root_(std::make_unique<Node>(true)) {}

    void insert(int64_t key, RID rid);
    bool search(int64_t key, RID& out) const;
    std::vector<std::pair<int64_t, RID>> rangeScan(int64_t low, int64_t high) const;
    bool erase(int64_t key);

    int height() const;
    std::vector<std::pair<int64_t, RID>> items() const;  // sorted, via leaf chain

private:
    struct Node {
        bool leaf;
        std::vector<int64_t> keys;
        std::vector<std::unique_ptr<Node>> children;  // internal nodes
        std::vector<RID> rids;                         // leaf nodes
        Node* next = nullptr;                          // leaf chain (non-owning)
        explicit Node(bool isLeaf) : leaf(isLeaf) {}
    };

    int order_;
    std::unique_ptr<Node> root_;

    Node* findLeaf(int64_t key) const;
    Node* leftmostLeaf() const;
    void insertNonFull(Node* node, int64_t key, RID rid);
    void splitChild(Node* parent, int index);
};

}  // namespace minidb
