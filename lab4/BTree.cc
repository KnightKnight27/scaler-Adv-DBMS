#include "BTree.h"

#include <iostream>

BTreeNode::BTreeNode(int t, bool leaf)
    : leaf(leaf), t(t) {}

BTreeNode::~BTreeNode() {
    for (auto child : children) {
        delete child;
    }
}

BTreeNode* BTreeNode::search(int key) {
    int i = 0;

    while (i < static_cast<int>(keys.size()) && key > keys[i]) {
        ++i;
    }

    if (i < static_cast<int>(keys.size()) && keys[i] == key) {
        return this;
    }

    if (leaf) {
        return nullptr;
    }

    return children[i]->search(key);
}

void BTreeNode::traverse() const {
    size_t i;

    for (i = 0; i < keys.size(); ++i) {
        if (!leaf) {
            children[i]->traverse();
        }

        std::cout << keys[i] << ' ';
    }

    if (!leaf) {
        children[i]->traverse();
    }
}

void BTreeNode::splitChild(int index, BTreeNode* child) {
    BTreeNode* sibling = new BTreeNode(child->t, child->leaf);

    int median = child->keys[t - 1];

    for (int j = t; j < 2 * t - 1; ++j) {
        sibling->keys.push_back(child->keys[j]);
    }

    if (!child->leaf) {
        for (int j = t; j < 2 * t; ++j) {
            sibling->children.push_back(child->children[j]);
        }

        child->children.resize(t);
    }

    child->keys.resize(t - 1);

    children.insert(children.begin() + index + 1, sibling);
    keys.insert(keys.begin() + index, median);
}

void BTreeNode::insertNonFull(int key) {
    int i = static_cast<int>(keys.size()) - 1;

    if (leaf) {
        keys.push_back(0);

        while (i >= 0 && keys[i] > key) {
            keys[i + 1] = keys[i];
            --i;
        }

        keys[i + 1] = key;
        return;
    }

    while (i >= 0 && keys[i] > key) {
        --i;
    }

    ++i;

    if (children[i]->keys.size() == static_cast<size_t>(2 * t - 1)) {
        splitChild(i, children[i]);

        if (key > keys[i]) {
            ++i;
        }
    }

    children[i]->insertNonFull(key);
}

BTree::BTree(int minDegree)
    : root(nullptr), t(minDegree) {}

BTree::~BTree() {
    delete root;
}

BTreeNode* BTree::search(int key) {
    if (!root) {
        return nullptr;
    }

    return root->search(key);
}

void BTree::insert(int key) {
    if (!root) {
        root = new BTreeNode(t, true);
        root->keys.push_back(key);
        return;
    }

    if (root->keys.size() == static_cast<size_t>(2 * t - 1)) {
        BTreeNode* newRoot = new BTreeNode(t, false);

        newRoot->children.push_back(root);
        newRoot->splitChild(0, root);

        int childIndex = 0;

        if (key > newRoot->keys[0]) {
            childIndex = 1;
        }

        newRoot->children[childIndex]->insertNonFull(key);

        root = newRoot;
    } else {
        root->insertNonFull(key);
    }
}

void BTree::traverse() const {
    if (root) {
        root->traverse();
    }

    std::cout << '\n';
}
