#ifndef BTREE_H
#define BTREE_H

#include <vector>
#include <iostream>

class BTreeNode {
public:
    bool leaf;
    std::vector<int> keys;
    std::vector<BTreeNode*> children;
    int t;

    BTreeNode(int _t, bool _leaf);
    void traverse(std::ostream &out);
    void insertNonFull(int k);
    void splitChild(int i, BTreeNode* y);
};

class BTree {
public:
    BTreeNode* root;
    int t;
    BTree(int _t);
    void traverse(std::ostream &out);
    void insert(int k);
};

#endif // BTREE_H
