// Lab 6 - B-Tree (header)
// Rama Krishnan (24BCS10087) <rama.24bcs10087@sst.scaler.com>
//
// A textbook B-Tree of minimum degree t (every non-root node holds between
// t-1 and 2t-1 keys). Operations: insert (pre-emptive split on the way
// down), erase (predecessor/successor swap + borrow-or-merge underflow
// repair), and search. Used as the canonical on-disk index structure in
// production engines (PostgreSQL nbtree, MySQL InnoDB, SQLite).

#ifndef BTREE_H
#define BTREE_H

#include <vector>
#include <queue>

struct TreeNode
{
    bool leaf;
    std::vector<int> values;
    std::vector<TreeNode *> children;

    TreeNode(bool isLeaf);
};

class BalancedTree
{
    TreeNode *rootNode;
    int min_degree; // minimum degree t

    // private helpers
    void split_child(TreeNode *parent, int index, TreeNode *child);
    void insert_nonfull(TreeNode *node, int key);
    bool contains(TreeNode *node, int key);
    void print_inorder(TreeNode *node);
    void collect_inorder(TreeNode *node, std::vector<int> &out);
    int key_index(TreeNode *node, int key);
    void remove_leaf(TreeNode *node, int idx);
    int predecessor(TreeNode *node, int idx);
    int successor(TreeNode *node, int idx);
    void merge_nodes(TreeNode *node, int idx);
    void borrow_prev(TreeNode *node, int idx);
    void borrow_next(TreeNode *node, int idx);
    void fill_child(TreeNode *node, int idx);
    void remove_internal(TreeNode *node, int idx);
    void remove_node(TreeNode *node, int key);
    void free_node(TreeNode *node);

public:
    BalancedTree(int t);
    ~BalancedTree();

    void insert(int key);
    void remove(int key);
    bool search(int key);

    void print_inorder();
    void print_levels();

    // test helper: collect inorder keys into vector
    void collect_inorder(std::vector<int> &out);
};

#endif // BTREE_H
