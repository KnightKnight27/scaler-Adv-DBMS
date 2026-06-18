#ifndef BTREE_H
#define BTREE_H

#include <iostream>

#define DEGREE 3  // minimum degree t=3

class BTree {
public:
    struct Node {
        int keys[2 * DEGREE - 1];
        Node *children[2 * DEGREE];
        int n;      // current number of keys
        bool leaf;

        Node(bool isLeaf);
    };

    BTree();
    ~BTree();

    void insert(int key);
    bool find(int key);
    void print();

private:
    Node *root;

    void splitChild(Node *parent, int i, Node *child);
    void insertNonFull(Node *node, int key);
    bool findHelper(Node *node, int key);
    void traverse(Node *node, int depth);
    void cleanup(Node *node);
};

#endif
