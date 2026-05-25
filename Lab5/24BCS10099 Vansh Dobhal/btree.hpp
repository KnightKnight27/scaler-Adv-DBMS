#ifndef BTREE_HPP
#define BTREE_HPP

#include <vector>

const int BTREE_MIN_DEGREE = 3;

class BTreeNode {
public:
    explicit BTreeNode(bool leafNode);
    ~BTreeNode();

    void traverse() const;
    const BTreeNode* search(int key) const;
    void collectLevels(std::vector<std::vector<std::vector<int>>>& levels,
                       int depth) const;

private:
    int keys[2 * BTREE_MIN_DEGREE - 1];
    BTreeNode* children[2 * BTREE_MIN_DEGREE];
    int keyCount;
    bool leaf;

    void insertNonFull(int key);
    void splitChild(int childIndex, BTreeNode* fullChild);

    friend class BTree;
};

class BTree {
public:
    BTree();
    ~BTree();

    void insert(int key);
    void traverse() const;
    const BTreeNode* search(int key) const;
    void printStructure() const;

private:
    BTreeNode* root;
};

#endif