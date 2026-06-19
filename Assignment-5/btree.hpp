#ifndef BTREE_HPP
#define BTREE_HPP

#include <iostream>
#include <string>

// BTreeNode — one node in the B-Tree.
// Each node holds between t-1 and 2t-1 keys.
// Leaf nodes have no children (all C[i] == nullptr).
class BTreeNode {
public:
    int *keys;
    int t;              // minimum degree
    BTreeNode **C;      // child pointers, size 2t
    int n;              // current key count
    bool leaf;

    BTreeNode(int _t, bool _leaf);
    ~BTreeNode();

    void traverse();
    BTreeNode* search(int k);

    // splits child C[i] when it's full (has 2t-1 keys)
    void splitChild(int i, BTreeNode *y);

    // inserts key k into a non-full subtree rooted here
    void insertNonFull(int k);

    void printHelper(const std::string& indent, bool last);

    friend class BTree;
};

class BTree {
private:
    BTreeNode *root;
    int t;

    void destroySubtree(BTreeNode* node);

public:
    BTree(int _t = 2) : root(nullptr), t(_t) {}
    ~BTree();

    void insert(int k);
    BTreeNode* search(int k) {
        return root ? root->search(k) : nullptr;
    }
    void traverse() {
        if (root) root->traverse();
        std::cout << "\n";
    }
    void printTree();
};

#endif
