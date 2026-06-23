#ifndef BTREE_HPP
#define BTREE_HPP

#include <iostream>
#include <vector>
#include <string>

// A B-Tree Node class
class BTreeNode {
public:
    int *keys;          // Array of keys
    int t;              // Minimum degree (defines the range for number of keys)
    BTreeNode **C;      // Array of child pointers
    int n;              // Current number of keys
    bool leaf;          // Is true when node is leaf. Otherwise false

    BTreeNode(int _t, bool _leaf);
    ~BTreeNode();

    // A utility function to insert a new key in the subtree rooted with this node.
    // The node must be non-full when this function is called.
    void insertNonFull(int k);

    // A utility function to split the child y of this node.
    // i is the index of y in parent's child array C. The Child y must be full when this function is called.
    void splitChild(int i, BTreeNode *y);

    // A function to traverse all nodes in a subtree rooted with this node
    void traverse();

    // A function to search a key in the subtree rooted with this node.
    BTreeNode* search(int k);

    // Helper for printing tree hierarchically
    void printTreeHelper(const std::string& indent, bool last);

    friend class BTree;
};

// A B-Tree class
class BTree {
private:
    BTreeNode *root; // Pointer to root node
    int t;           // Minimum degree

public:
    // Constructor (Initializes tree as empty)
    BTree(int _t = 3) {
        root = nullptr;
        t = _t;
    }

    ~BTree();
    void deleteTree(BTreeNode* node);

    // Function to traverse the tree
    void traverse() {
        if (root != nullptr) root->traverse();
        std::cout << std::endl;
    }

    // Function to search a key in this tree
    BTreeNode* search(int k) {
        return (root == nullptr) ? nullptr : root->search(k);
    }

    // The main function that inserts a new key in this B-Tree
    void insert(int k);

    // Visualizes B-Tree hierarchically
    void printTree();
};

#endif // BTREE_HPP
