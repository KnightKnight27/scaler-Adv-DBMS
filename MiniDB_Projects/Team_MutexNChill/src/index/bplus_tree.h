#pragma once
#include "../storage/heap_file.h"
#include <vector>

// Minimum degree t=2 means:
//   max keys per node = 2t-1 = 3
//   min keys per non-root node = t-1 = 1
static const int BP_T = 2;

// A node in the B+ Tree.
// Leaf nodes:     keys[i] -> rids[i], plus a 'next' pointer to the next leaf.
// Internal nodes: keys[i] is a separator; children[i] is left of keys[i],
//                 children[keys.size()] is the rightmost child.
struct BPNode {
    bool             is_leaf;
    std::vector<int> keys;
    std::vector<RID> rids;          // only used in leaf nodes
    std::vector<BPNode*> children;  // only used in internal nodes
    BPNode*          next;          // leaf chain pointer (leaves only)

    explicit BPNode(bool leaf) : is_leaf(leaf), next(nullptr) {}
};

// B+ Tree mapping integer primary keys to RIDs.
// Key design choice vs plain B-Tree:
//   - All (key, RID) data lives only in leaf nodes.
//   - Internal nodes hold only routing keys (copies of leaf keys).
//   - Leaves are linked, enabling efficient range scans.
class BPlusTree {
public:
    BPlusTree();
    ~BPlusTree();

    void insert(int key, RID rid);

    // Returns false if key is not present.
    bool search(int key, RID& out);

    // Remove a key. Note: we remove from the leaf but do not rebalance
    // (safe for correctness at demo scale; production would need merging).
    void remove(int key);

    // Return all RIDs whose key is in [lo, hi] (inclusive).
    std::vector<RID> rangeSearch(int lo, int hi);

    bool contains(int key);

private:
    BPNode* root;
    int     max_keys; // = 2*BP_T - 1

    BPNode* findLeaf(int key);

    // Recursive insert. Returns {push_up_key, new_right_child} if the node
    // split, or {0, nullptr} if no split happened.
    struct Split { int key; BPNode* right; };
    Split   insertRec(BPNode* node, int key, RID rid);

    void    freeNode(BPNode* node);
};
