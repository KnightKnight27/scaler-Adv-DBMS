#pragma once

#include <iostream>


// Types & Core Structures


enum Color { RED, BLACK };

struct Node {
    int data;
    Color color;
    Node* left;
    Node* right;
    Node* parent;
};


// RBTree Class Declaration

class RBTree {
private:
    Node* root;                         // Pointer to the root node of the tree
    Node* TNULL;                        // Sentinel node representing null leaves

    // Internal utility and traversal helpers
    void initializeNULLNode(Node* node, Node* parent);
    void preOrderHelper(Node* node) const;
    
    // Node rotation primitives
    void leftRotate(Node* x);
    void rightRotate(Node* x);
    
    // Balancing and structural correction helpers
    void insertFixup(Node* k);
    void deleteFixup(Node* x);
    void transplant(Node* u, Node* v);
    Node* minimum(Node* node);

    // Deep memory allocation cleanup
    void destroyTree(Node* node);

public:
    // Lifecycle Management
    RBTree();
    ~RBTree();

    // Public Application Interface
    void insert(int key);
    void deleteNode(int key);
    Node* searchTree(int key);
    void printTree() const;
};