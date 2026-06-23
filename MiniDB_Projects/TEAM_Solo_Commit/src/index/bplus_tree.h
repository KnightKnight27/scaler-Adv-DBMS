// MiniDB - B+Tree index mapping a column Value to the RID(s) of matching rows.
//
// This grows out of the B-Tree I built in Lab 4 (proactive node split on overflow) and turns
// it into a B+Tree: all data lives in the leaves, leaves are chained left-to-right for range
// scans, and internal nodes hold only separator keys for routing. Non-unique secondary indexes
// are supported by storing a list of RIDs per key. The tree is in-memory and is rebuilt from
// the heap when a database is reopened (indexes are derived data, not the source of truth).
#pragma once

#include <vector>

#include "../common/rid.h"
#include "../common/types.h"

namespace minidb {

class BPlusTree {
public:
    BPlusTree() : root_(new Node(true)) {}
    ~BPlusTree() { Destroy(root_); }

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    void Insert(const Value& key, const RID& rid);
    // All RIDs stored under `key` (empty if absent).
    std::vector<RID> Search(const Value& key) const;
    // RIDs whose key is in [low, high] inclusive, via the leaf chain.
    std::vector<RID> RangeScan(const Value& low, const Value& high) const;
    void Remove(const Value& key, const RID& rid);

    int Height() const { return HeightOf(root_); }

private:
    static constexpr int ORDER = 4;          // max children of an internal node
    static constexpr int MAX_KEYS = ORDER - 1;

    struct Node {
        bool leaf;
        std::vector<Value> keys;
        std::vector<std::vector<RID>> vals;  // leaf only, parallel to keys
        std::vector<Node*> kids;             // internal only
        Node* next = nullptr;                // leaf chain
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
    };

    // Recursive insert. On split, sets *promo / *right and returns true.
    bool InsertRec(Node* n, const Value& key, const RID& rid, Value* promo, Node** right);
    const Node* FindLeaf(const Value& key) const;
    static int ChildIndex(const Node* n, const Value& key);
    static int HeightOf(const Node* n);
    static void Destroy(Node* n);

    Node* root_;
};

}  // namespace minidb
