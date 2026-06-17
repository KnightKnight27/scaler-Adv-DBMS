// BTree.h — ADBMS Lab 6, 24BCS10115 Gauri Shukla
//
// An in-memory B-Tree of minimum degree t, holding a set of distinct int
// keys (duplicates are ignored). Declaration only; the algorithms live in
// BTree.cc. This mirrors the .h/.cc split I used for the Red-Black Tree in
// Lab 5 — the B-Tree is the on-disk cousin of that in-memory balanced tree.
//
// Invariants maintained for every node (root is the only exception to the
// lower bound):
//   * t - 1 <= #keys <= 2t - 1
//   * an internal node with k keys has exactly k + 1 children
//   * keys inside a node are strictly increasing
//   * all leaves sit at the same depth
//
// Public surface: insert / remove / contains / inorder / print / validate.

#ifndef ADBMS_LAB6_BTREE_H
#define ADBMS_LAB6_BTREE_H

#include <iosfwd>
#include <string>
#include <vector>

class BTree {
public:
    explicit BTree(int min_degree = 3);
    ~BTree();

    // No copying — the tree owns raw Node pointers.
    BTree(const BTree&)            = delete;
    BTree& operator=(const BTree&) = delete;

    void insert(int key);     // inserts key; no effect if it already exists
    bool remove(int key);     // returns true iff a key was actually removed
    bool contains(int key) const;

    int  size()   const { return count_; }
    bool empty()  const { return count_ == 0; }
    int  degree() const { return t_; }       // the minimum degree t
    int  height() const;                      // 0 for an empty tree

    std::vector<int> inorder() const;         // keys in sorted order
    void print(std::ostream& os) const;       // level-order picture of the tree
    std::string validate() const;             // "" if healthy, else first fault

private:
    struct Node {
        bool               leaf;
        std::vector<int>   keys;
        std::vector<Node*> kids;
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
        int  num() const { return static_cast<int>(keys.size()); }
        bool full(int t) const { return num() == 2 * t - 1; }
    };

    Node* root_  = nullptr;
    int   t_;
    int   count_ = 0;

    // --- shared helpers -------------------------------------------------
    static void free_subtree(Node* n);                 // recursive teardown
    int  first_ge(const Node* n, int key) const;       // lower bound slot

    // --- insert side ----------------------------------------------------
    void split_child(Node* parent, int idx);           // parent->kids[idx] is full
    void insert_nonfull(Node* n, int key);

    // --- remove side (CLRS chapter 18.3) --------------------------------
    bool remove_from(Node* n, int key);                // n has >= t keys (or is root)
    void ensure_fat(Node* parent, int idx);            // make kids[idx] have >= t keys
    void rotate_from_left(Node* parent, int idx);      // borrow from left sibling
    void rotate_from_right(Node* parent, int idx);     // borrow from right sibling
    void merge_children(Node* parent, int idx);        // fuse kids[idx] + kids[idx+1]
    int  rightmost_key(Node* n) const;                 // predecessor source
    int  leftmost_key(Node* n) const;                  // successor source

    // --- read-only walks ------------------------------------------------
    int  height_of(const Node* n) const;
    void collect_inorder(const Node* n, std::vector<int>& out) const;
    std::string check(const Node* n, bool is_root, int depth, int& leaf_depth,
                      bool have_lo, int lo, bool have_hi, int hi) const;
};

#endif  // ADBMS_LAB6_BTREE_H
