#ifndef BTREE_HPP
#define BTREE_HPP

#include <iostream>
#include <string>

class BTree;

// A B-Tree node
class BTreeNode {
private:
    int* keys;      // Array of keys
    int t;          // Minimum degree (defines range for number of keys)
    BTreeNode** C;  // Array of child pointers
    int n;          // Current number of keys
    bool leaf;      // Is true when node is leaf. Otherwise false

public:
    BTreeNode(int t, bool leaf);
    ~BTreeNode();

    // A function to traverse all nodes in a subtree rooted with this node (in-order)
    void traverse();

    // A function to search a key in subtree rooted with this node.
    // Returns the node if key is found, and sets index to the key's position in that node.
    BTreeNode* search(int k, int& index);

    // A utility function to insert a new key in the subtree rooted with
    // this node. The node must be non-full when this function is called.
    void insertNonFull(int k);

    // A utility function to split the child y of this node.
    // i is the index of y in child array C[]. The Child y must be full when this function is called.
    void splitChild(int i, BTreeNode* y);

    // Helper function to print tree structure visually
    void printVisual(const std::string& prefix);

    // Friend class to allow BTree access to private members
    friend class BTree;
};

// A B-Tree
class BTree {
private:
    BTreeNode* root; // Pointer to root node
    int t;           // Minimum degree

public:
    // Constructor (Initializes tree as empty)
    BTree(int temp_t) {
        root = nullptr;
        t = temp_t;
    }

    // Destructor to free all allocated memory
    ~BTree();

    // Function to traverse the tree (in-order traversal)
    void traverse();

    // Function to search a key in this tree.
    // Returns the node if key is found, and sets index to key's position.
    BTreeNode* search(int k, int& index);

    // The main function that inserts a new key in this B-Tree
    void insert(int k);

    // Prints a visual structure of the B-Tree
    void printVisual();

    // Check if the tree is empty
    bool isEmpty() const {
        return root == nullptr;
    }

    // Get the minimum degree
    int getMinDegree() const {
        return t;
    }
};

#endif // BTREE_HPP
