#ifndef RED_BLACK_TREE_H
#define RED_BLACK_TREE_H

#include <iostream>
#include <string>

// Colors of a node in a Red-Black Tree
enum Color { RED, BLACK };

// Node structure of the Red-Black Tree
struct Node {
    int data;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    // Constructor for a new node
    Node(int val) : data(val), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

// Red-Black Tree class implementing standard CLRS operations
class RedBlackTree {
private:
    Node* root;
    Node* nil; // Sentinel node representing leaf nodes

    // Helper functions for internal operations
    void leftRotate(Node* x);
    void rightRotate(Node* x);
    void fixInsert(Node* k);
    
    // Traversals and printing helpers
    void inorderHelper(Node* node) const;
    void printTreeHelper(Node* node, const std::string& indent, bool last) const;
    
    // Memory cleanup helper
    void destroyTree(Node* node);

public:
    // Constructor and Destructor
    RedBlackTree();
    ~RedBlackTree();

    // Core Red-Black Tree API
    void insert(int key);
    void inorder() const;
    void printTree() const;
    
    // Getter for the root node (useful for verification)
    Node* getRoot() const;
    
    // Helper to check if a node is NIL
    bool isNil(Node* node) const;
};

#endif // RED_BLACK_TREE_H
