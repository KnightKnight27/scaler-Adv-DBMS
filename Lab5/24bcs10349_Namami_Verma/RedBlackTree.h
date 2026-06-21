#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H

#include <iostream>
using namespace std;

enum Color { RED, BLACK };

struct Node {
    int data;
    Color color;
    Node *left, *right, *parent;

    Node(int data) {
        this->data = data;
        left = right = parent = nullptr;
        color = RED;
    }
};

class RedBlackTree {
private:
    Node* root;

    void rotateLeft(Node*& root, Node*& pt);
    void rotateRight(Node*& root, Node*& pt);
    void fixInsert(Node*& root, Node*& pt);
    void inorderHelper(Node* root);

public:
    RedBlackTree() { root = nullptr; }

    void insert(int data);
    void inorder();
};

#endif