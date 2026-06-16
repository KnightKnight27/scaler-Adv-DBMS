#pragma once
// bplustree.h — In-memory B+ Tree used as the primary-key index.
//
// Key properties:
//   • Internal nodes store separator keys and child pointers only.
//   • Leaf nodes store (key, RID) pairs and are linked in sorted order
//     so range scans walk the leaf level without touching internal nodes.
//   • A configurable `order` controls the maximum keys per node.
//     A node splits when it overflows during insert.
//   • Deletes are lazy: the key is removed from the leaf but no
//     rebalancing is done (acceptable for our teaching scope; noted in Limitations).
#include "value.h"
#include <vector>

namespace minidb {

class BPlusTree {
public:
    // order = max keys per leaf node.  Internal nodes use order+1 children.
    explicit BPlusTree(int order = 32);
    ~BPlusTree();

    void insert(const Value& key, RID rid);       // may split nodes
    bool search(const Value& key, RID& out) const; // exact lookup; false = not found
    bool erase (const Value& key);                 // lazy delete; false = not found
    std::vector<RID> range(const Value& lo, const Value& hi) const;

    int height() const;    // useful for EXPLAIN output
    int count()  const { return count_; }

private:
    struct Node {
        bool          leaf;
        std::vector<Value> keys;
        // Internal nodes: children[i] is the subtree for keys < keys[i]
        //                  children[keys.size()] is the right-most subtree
        std::vector<Node*> children;
        // Leaf nodes: rids[i] corresponds to keys[i]
        std::vector<RID>   rids;
        Node*              next = nullptr;  // leaf-chain for range scans

        explicit Node(bool l) : leaf(l) {}
    };

    // Recursive insert; returns a new child to push up if a split happened.
    // split_key and split_child are output params set when a split occurs.
    bool insert_rec(Node* node, const Value& key, RID rid,
                    Value& split_key, Node*& split_child);  // NOLINT: Node visible inside class

    Node* find_leaf(const Value& key) const;  // descend to the correct leaf
    void  destroy  (Node* n);                  // recursive free

    Node* root_;
    int   order_;   // max keys per leaf node
    int   count_ = 0;
};

} // namespace minidb
