#pragma once
#include <iostream>
#include <vector>

class BTreeNode {
public:
    std::vector<int> keys;
    std::vector<BTreeNode*> children;
    int t;      // Minimum degree
    bool leaf;

    BTreeNode(int _t, bool _leaf);
    
    void traverse();
    BTreeNode* search(int k);
    void insertNonFull(int k);
    void splitChild(int i, BTreeNode* y);
    
    // Deletion functions
    void remove(int k);
    void removeFromLeaf(int idx);
    void removeFromNonLeaf(int idx);
    int getPred(int idx);
    int getSucc(int idx);
    void fill(int idx);
    void borrowFromPrev(int idx);
    void borrowFromNext(int idx);
    void merge(int idx);

    friend class BTree;
};

class BTree {
private:
    BTreeNode* root;
    int t; // Minimum degree

public:
    BTree(int _t);
    void traverse();
    BTreeNode* search(int k);
    void insert(int k);
    void remove(int k);
};