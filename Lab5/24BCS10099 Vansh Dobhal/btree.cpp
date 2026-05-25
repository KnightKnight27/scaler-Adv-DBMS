#include "btree.hpp"

#include <iostream>

BTreeNode::BTreeNode(bool leafNode) : keyCount(0), leaf(leafNode) {
    for (int i = 0; i < 2 * BTREE_MIN_DEGREE; ++i) {
        children[i] = nullptr;
    }
}

BTreeNode::~BTreeNode() {
    if (!leaf) {
        for (int i = 0; i <= keyCount; ++i) {
            delete children[i];
        }
    }
}

void BTreeNode::traverse() const {
    int i = 0;
    for (; i < keyCount; ++i) {
        if (!leaf) {
            children[i]->traverse();
        }
        std::cout << keys[i] << ' ';
    }

    if (!leaf) {
        children[i]->traverse();
    }
}

const BTreeNode* BTreeNode::search(int key) const {
    int i = 0;
    while (i < keyCount && key > keys[i]) {
        ++i;
    }

    if (i < keyCount && keys[i] == key) {
        return this;
    }

    if (leaf) {
        return nullptr;
    }

    return children[i]->search(key);
}

void BTreeNode::splitChild(int childIndex, BTreeNode* fullChild) {
    BTreeNode* sibling = new BTreeNode(fullChild->leaf);
    sibling->keyCount = BTREE_MIN_DEGREE - 1;

    for (int j = 0; j < BTREE_MIN_DEGREE - 1; ++j) {
        sibling->keys[j] = fullChild->keys[j + BTREE_MIN_DEGREE];
    }

    if (!fullChild->leaf) {
        for (int j = 0; j < BTREE_MIN_DEGREE; ++j) {
            sibling->children[j] = fullChild->children[j + BTREE_MIN_DEGREE];
            fullChild->children[j + BTREE_MIN_DEGREE] = nullptr;
        }
    }

    fullChild->keyCount = BTREE_MIN_DEGREE - 1;

    for (int j = keyCount; j >= childIndex + 1; --j) {
        children[j + 1] = children[j];
    }
    children[childIndex + 1] = sibling;

    for (int j = keyCount - 1; j >= childIndex; --j) {
        keys[j + 1] = keys[j];
    }
    keys[childIndex] = fullChild->keys[BTREE_MIN_DEGREE - 1];
    ++keyCount;
}

void BTreeNode::insertNonFull(int key) {
    int i = keyCount - 1;

    if (leaf) {
        while (i >= 0 && keys[i] > key) {
            keys[i + 1] = keys[i];
            --i;
        }
        keys[i + 1] = key;
        ++keyCount;
        return;
    }

    while (i >= 0 && keys[i] > key) {
        --i;
    }
    ++i;

    if (children[i]->keyCount == 2 * BTREE_MIN_DEGREE - 1) {
        splitChild(i, children[i]);
        if (key > keys[i]) {
            ++i;
        }
    }

    children[i]->insertNonFull(key);
}

void BTreeNode::collectLevels(
    std::vector<std::vector<std::vector<int>>>& levels,
    int depth) const {
    if (static_cast<int>(levels.size()) == depth) {
        levels.push_back({});
    }

    std::vector<int> nodeKeys;
    for (int i = 0; i < keyCount; ++i) {
        nodeKeys.push_back(keys[i]);
    }
    levels[depth].push_back(nodeKeys);

    if (!leaf) {
        for (int i = 0; i <= keyCount; ++i) {
            children[i]->collectLevels(levels, depth + 1);
        }
    }
}

BTree::BTree() : root(new BTreeNode(true)) {}

BTree::~BTree() {
    delete root;
}

void BTree::insert(int key) {
    if (root->keyCount == 2 * BTREE_MIN_DEGREE - 1) {
        BTreeNode* newRoot = new BTreeNode(false);
        newRoot->children[0] = root;
        newRoot->splitChild(0, root);

        int childIndex = key > newRoot->keys[0] ? 1 : 0;
        newRoot->children[childIndex]->insertNonFull(key);
        root = newRoot;
        return;
    }

    root->insertNonFull(key);
}

void BTree::traverse() const {
    std::cout << "B-Tree inorder: ";
    root->traverse();
    std::cout << '\n';
}

const BTreeNode* BTree::search(int key) const {
    return root->search(key);
}

void BTree::printStructure() const {
    std::vector<std::vector<std::vector<int>>> levels;
    root->collectLevels(levels, 0);

    std::cout << "\nB-Tree structure (minimum degree " << BTREE_MIN_DEGREE << ")\n";
    for (std::size_t level = 0; level < levels.size(); ++level) {
        std::cout << "Level " << level << ": ";
        for (const auto& node : levels[level]) {
            std::cout << '[';
            for (std::size_t i = 0; i < node.size(); ++i) {
                std::cout << node[i];
                if (i + 1 < node.size()) {
                    std::cout << ", ";
                }
            }
            std::cout << "] ";
        }
        std::cout << '\n';
    }
}