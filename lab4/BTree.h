#ifndef BTREE_H
#define BTREE_H

#include <vector>

class BTreeNode {
public:
    bool leaf;
    int t;

    std::vector<int> keys;
    std::vector<BTreeNode*> children;

    BTreeNode(int t, bool leaf);
    ~BTreeNode();

    BTreeNode* search(int key);
    void traverse() const;

    void insertNonFull(int key);
    void splitChild(int index, BTreeNode* child);

private:
    friend class BTree;
};

class BTree {
public:
    explicit BTree(int minDegree);
    ~BTree();

    BTreeNode* search(int key);
    void insert(int key);
    void traverse() const;

private:
    BTreeNode* root;
    int t;
};

#endif
