#pragma once
#include <iostream>
#include <vector>
using namespace std;

// ADBMS LAB 6
// Roll No: 24BCS10193
// Name: Anushka Jain

class BTreeNode {
public:
    vector<int> keys;
    vector<BTreeNode*> children;
    int degree;
    bool leaf;

    BTreeNode(int t, bool isLeaf) {
        degree = t;
        leaf = isLeaf;
    }

    void traverse() {
        int i;
        for(i = 0; i < keys.size(); i++) {
            if(!leaf)
                children[i]->traverse();
            cout << keys[i] << " ";
        }

        if(!leaf)
            children[i]->traverse();
    }

    BTreeNode* search(int key) {
        int i = 0;
        while(i < keys.size() && key > keys[i])
            i++;

        if(i < keys.size() && keys[i] == key)
            return this;

        if(leaf)
            return nullptr;

        return children[i]->search(key);
    }

    void splitChild(int i, BTreeNode* node);
    void insertNonFull(int key);
};

class BTree {
public:
    BTreeNode* root;
    int degree;

    BTree(int t) {
        root = nullptr;
        degree = t;
    }

    void traverse() {
        if(root)
            root->traverse();
    }

    BTreeNode* search(int key) {
        return (root == nullptr) ? nullptr : root->search(key);
    }

    void insert(int key);
};

void BTreeNode::insertNonFull(int key) {
    int i = keys.size() - 1;

    if(leaf) {
        keys.push_back(0);

        while(i >= 0 && keys[i] > key) {
            keys[i + 1] = keys[i];
            i--;
        }

        keys[i + 1] = key;
    }
    else {
        while(i >= 0 && keys[i] > key)
            i--;

        if(children[i + 1]->keys.size() == 2 * degree - 1) {
            splitChild(i + 1, children[i + 1]);

            if(keys[i + 1] < key)
                i++;
        }

        children[i + 1]->insertNonFull(key);
    }
}

void BTreeNode::splitChild(int i, BTreeNode* node) {
    BTreeNode* newNode = new BTreeNode(node->degree, node->leaf);

    for(int j = 0; j < degree - 1; j++)
        newNode->keys.push_back(node->keys[j + degree]);

    if(!node->leaf) {
        for(int j = 0; j < degree; j++)
            newNode->children.push_back(node->children[j + degree]);
    }

    int middleKey = node->keys[degree - 1];

    node->keys.resize(degree - 1);
    if(!node->leaf)
        node->children.resize(degree);

    children.insert(children.begin() + i + 1, newNode);
    keys.insert(keys.begin() + i, middleKey);
}

void BTree::insert(int key) {
    if(root == nullptr) {
        root = new BTreeNode(degree, true);
        root->keys.push_back(key);
    }
    else {
        if(root->keys.size() == 2 * degree - 1) {
            BTreeNode* newRoot = new BTreeNode(degree, false);

            newRoot->children.push_back(root);
            newRoot->splitChild(0, root);

            int i = 0;
            if(newRoot->keys[0] < key)
                i++;

            newRoot->children[i]->insertNonFull(key);

            root = newRoot;
        }
        else {
            root->insertNonFull(key);
        }
    }
}