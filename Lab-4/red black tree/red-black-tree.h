#pragma once
#ifndef RED_BLACK_TREE_H
#define RED_BLACK_TREE_H

#include <iostream>

class RedBlackTree {
public:
    enum Color { RED, BLACK };

    RedBlackTree();
    ~RedBlackTree();

    void insert(int key);
    void remove(int key);
    bool contains(int key) const;
    void printInOrder(std::ostream &out) const;

private:
    struct Node {
        Node(int k = 0, Color c = RED) : key(k), color(c), left(nullptr), right(nullptr), parent(nullptr) {}

        int key;
        Color color;
        Node *left;
        Node *right;
        Node *parent;
    };

    Node *root_;
    Node *nil_; // sentinel

    // helpers
    void destroy(Node *node);
    void leftRotate(Node *x);
    void rightRotate(Node *y);
    void insertFixup(Node *node);
    void deleteFixup(Node *x);
    void transplant(Node *u, Node *v);
    Node *minimum(Node *node) const;
    Node *search(Node *node, int key) const;
    void printInOrder(Node *node, std::ostream &out) const;
};

#endif // RED_BLACK_TREE_H
