#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H

#include <iostream>

enum Color { RED, BLACK };

struct RBNode {
    int key;
    Color color;
    RBNode *left, *right, *parent;
    RBNode(int k) : key(k), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RBTree {
public:
    RBNode* root;
    RBTree();
    void insert(int key);
    void inorderPrint(std::ostream &out);
private:
    void rotateLeft(RBNode* x);
    void rotateRight(RBNode* x);
    void insertFixup(RBNode* z);
    void inorder(RBNode* node, std::ostream &out);
};

#endif // REDBLACKTREE_H
