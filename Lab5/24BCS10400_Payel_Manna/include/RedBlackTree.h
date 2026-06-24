#ifndef RED_BLACK_TREE_H
#define RED_BLACK_TREE_H

#include <iostream>
using namespace std;

enum Color {
    RED,
    BLACK
};

struct Node {
    int data;
    Color color;
    Node *left, *right, *parent;

    Node(int data) {
        this->data = data;
        color = RED;
        left = right = parent = nullptr;
    }
};

class RedBlackTree {
private:
    Node* root;

    void rotateLeft(Node*& pt);
    void rotateRight(Node*& pt);

    void fixInsert(Node*& pt);

    void inorderHelper(Node* root);

public:
    RedBlackTree();

    void insert(int data);

    void inorder();
};

#endif