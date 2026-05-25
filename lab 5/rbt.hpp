#ifndef RBT_HPP
#define RBT_HPP

#include <iostream>

enum Color { RED, BLACK };

struct Node {
    int    data;
    Color  color;
    Node*  left;
    Node*  right;
    Node*  parent;

    Node(int value)
        : data(value), color(RED),
          left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
private:
    Node* root;
    Node* NIL;   // Sentinel nil node (always BLACK)

    // Rotation helpers
    void rotateLeft(Node* x);
    void rotateRight(Node* x);

    // Fix RB properties after insertion
    void fixInsert(Node* z);

    // Recursive inorder helper
    void inorderHelper(Node* node);

    // Recursive tree-print helper
    void printHelper(Node* node, std::string indent, bool last);

public:
    RedBlackTree();

    void insert(int value);
    void inorder();
    void printTree();

    // Search — returns node or NIL
    Node* search(int value);
};

#endif // RBT_HPP