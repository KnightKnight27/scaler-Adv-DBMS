#pragma once
#include <iostream>

enum Color { RED, BLACK };

struct Node {
    int data;
    Color color;
    Node* left;
    Node* right;
    Node* parent;
};

class RBTree {
private:
    Node* root;
    Node* TNULL;

    void initializeNULLNode(Node* node, Node* parent);
    void preOrderHelper(Node* node) const;
    void leftRotate(Node* x);
    void rightRotate(Node* x);
    
    // Balancing helpers
    void insertFixup(Node* k);
    void deleteFixup(Node* x);
    void transplant(Node* u, Node* v);
    Node* minimum(Node* node);

    // Memory cleanup
    void destroyTree(Node* node);

public:
    RBTree();
    ~RBTree();

    void insert(int key);
    void deleteNode(int key);
    Node* searchTree(int key);
    void printTree() const;
};