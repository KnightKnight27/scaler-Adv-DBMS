#ifndef RED_BLACK_TREE_H
#define RED_BLACK_TREE_H

#include <iostream>
using namespace std;

enum Color { RED, BLACK };

struct Node {
    int data;
    Color color;
    Node *left, *right, *parent;

    Node(int value);
};

class RedBlackTree {
private:
    Node* root;

    void rotateLeft(Node* x);
    void rotateRight(Node* x);
    void fixInsert(Node* node);
    void inorderHelper(Node* node);
    void preorderHelper(Node* node);

public:
    RedBlackTree();

    void insert(int value);
    void inorder();
    void preorder();
};

#endif