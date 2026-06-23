#pragma once

#include <iostream>
#include <vector>


// BTreeNode Class
// Represents a single node in the B-Tree
class BTreeNode {
public:
    std::vector<int> keys;                  // Stores keys in sorted order
    std::vector<BTreeNode*> children;       // Pointers to child nodes

    int t;                                  // Minimum degree of the B-Tree
    bool leaf;                              // True if node is a leaf node

    // Constructor
    BTreeNode(int _t, bool _leaf);

    // Traversal and Search Operations
    void traverse();
    BTreeNode* search(int k);

    // Insertion Operations
    void insertNonFull(int k);
    void splitChild(int i, BTreeNode* y);

    // Deletion Operations
    void remove(int k);
    void removeFromLeaf(int idx);
    void removeFromNonLeaf(int idx);

    // Helper Functions for Deletion
    int getPred(int idx);                   // Find predecessor
    int getSucc(int idx);                   // Find successor
    void fill(int idx);                     // Ensure child has enough keys
    void borrowFromPrev(int idx);           // Borrow key from left sibling
    void borrowFromNext(int idx);           // Borrow key from right sibling
    void merge(int idx);                    // Merge two child nodes

    // Allow BTree class to access private members
    friend class BTree;
};


// BTree Class
// Main interface for B-Tree operations
class BTree {
private:
    BTreeNode* root;                        // Pointer to root node
    int t;                                  // Minimum degree of the tree

public:
    // Constructor
    BTree(int _t);

    // Public Operations
    void traverse();
    BTreeNode* search(int k);
    void insert(int k);
    void remove(int k);
};