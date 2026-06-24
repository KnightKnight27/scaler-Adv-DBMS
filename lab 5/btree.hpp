#ifndef BTREE_HPP
#define BTREE_HPP

#include <vector>
#include <iostream>

// Minimum degree (order) of the B-Tree.
// Each node (except root) has at least t-1 keys and at most 2t-1 keys.
const int T = 3;

class BTreeNode {
public:
    int     keys[2 * T - 1];       // Max keys per node
    BTreeNode* children[2 * T];    // Max children per node
    int     n;                     // Current number of keys
    bool    leaf;                  // True if leaf node

    BTreeNode(bool isLeaf);

    // Traverse all nodes rooted at this node
    void traverse();

    // Search a key in subtree rooted at this node
    BTreeNode* search(int key);

    // Insert a new key into a non-full node
    void insertNonFull(int key);

    // Split the child y at index i (y must be full)
    void splitChild(int i, BTreeNode* y);

    friend class BTree;
};

class BTree {
private:
    BTreeNode* root;

public:
    BTree();

    // Traverse the tree
    void traverse();

    // Search a key; returns nullptr if not found
    BTreeNode* search(int key);

    // Insert a key
    void insert(int key);

    // Print tree structure level-by-level
    void printTree();

private:
    void printLevel(BTreeNode* node, int level);
};

#endif // BTREE_HPP