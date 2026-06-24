#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H

#include <iostream>

class RedBlackTree {
public:
    enum Color { red = 0, black = 1 };

    struct Node {
        int val;
        Color color;
        Node *left, *right, *parent;

        Node(int v)
            : val(v), color(red), left(nullptr), right(nullptr), parent(nullptr)
        {}
    };

    RedBlackTree();
    ~RedBlackTree();

    void insert(int val);
    bool find(int val);
    void remove(int val);
    void print();

private:
    Node *root;
    Node *NIL;

    void fixInsert(Node *node);
    void fixDelete(Node *node);
    void rotateLeft(Node *node);
    void rotateRight(Node *node);
    void transplant(Node *u, Node *v);
    Node *minimum(Node *node);
    void printHelper(Node *node, std::string indent, bool last);
    void deleteTree(Node *node);
};

#endif
