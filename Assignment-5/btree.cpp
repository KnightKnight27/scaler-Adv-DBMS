// btree.cpp — Lab 5
// Tanishq Singh | 24BCS10303
//
// B-Tree with minimum degree t. Each node stores between t-1 and 2t-1 keys.
// Splitting happens proactively on the way down (insert-on-descent style),
// so we never have to walk back up the tree.

#include "btree.hpp"
#include <iostream>

BTreeNode::BTreeNode(int _t, bool _leaf) {
    t = _t;
    leaf = _leaf;
    keys = new int[2 * t - 1];
    C = new BTreeNode*[2 * t];
    n = 0;
    for (int i = 0; i < 2 * t; i++)
        C[i] = nullptr;
}

BTreeNode::~BTreeNode() {
    delete[] keys;
    delete[] C;
}

void BTreeNode::traverse() {
    int i;
    for (i = 0; i < n; i++) {
        if (!leaf)
            C[i]->traverse();
        std::cout << " " << keys[i];
    }
    if (!leaf)
        C[i]->traverse();
}

BTreeNode* BTreeNode::search(int k) {
    int i = 0;
    while (i < n && k > keys[i])
        i++;
    if (i < n && keys[i] == k)
        return this;
    if (leaf)
        return nullptr;
    return C[i]->search(k);
}

void BTreeNode::splitChild(int i, BTreeNode *y) {
    BTreeNode *z = new BTreeNode(y->t, y->leaf);
    z->n = t - 1;

    for (int j = 0; j < t - 1; j++)
        z->keys[j] = y->keys[j + t];

    if (!y->leaf) {
        for (int j = 0; j < t; j++) {
            z->C[j] = y->C[j + t];
            y->C[j + t] = nullptr;
        }
    }

    y->n = t - 1;

    // shift existing children to make room for z
    for (int j = n; j >= i + 1; j--)
        C[j + 1] = C[j];
    C[i + 1] = z;

    // shift existing keys to make room for the promoted key
    for (int j = n - 1; j >= i; j--)
        keys[j + 1] = keys[j];

    keys[i] = y->keys[t - 1];
    n++;
}

void BTreeNode::insertNonFull(int k) {
    int i = n - 1;

    if (leaf) {
        // scan right-to-left, shift larger keys over, drop k in place
        while (i >= 0 && keys[i] > k) {
            keys[i + 1] = keys[i];
            i--;
        }
        keys[i + 1] = k;
        n++;
    } else {
        // find which child subtree k belongs in
        while (i >= 0 && keys[i] > k)
            i--;
        i++;

        if (C[i]->n == 2 * t - 1) {
            splitChild(i, C[i]);
            // after split, the middle key moved up to keys[i]
            // decide which half gets k
            if (keys[i] < k)
                i++;
        }
        C[i]->insertNonFull(k);
    }
}

void BTree::insert(int k) {
    if (root == nullptr) {
        root = new BTreeNode(t, true);
        root->keys[0] = k;
        root->n = 1;
        return;
    }

    if (root->n == 2 * t - 1) {
        // root is full — tree grows in height
        BTreeNode *s = new BTreeNode(t, false);
        s->C[0] = root;
        s->splitChild(0, root);

        int i = (s->keys[0] < k) ? 1 : 0;
        s->C[i]->insertNonFull(k);
        root = s;
    } else {
        root->insertNonFull(k);
    }
}

BTree::~BTree() {
    if (root)
        destroySubtree(root);
}

void BTree::destroySubtree(BTreeNode* node) {
    if (!node) return;
    if (!node->leaf) {
        for (int i = 0; i <= node->n; i++)
            destroySubtree(node->C[i]);
    }
    delete node;
}

void BTreeNode::printHelper(const std::string& indent, bool last) {
    std::cout << indent;
    std::cout << (last ? "\033[1;30m└── \033[0m" : "\033[1;30m├── \033[0m");
    std::cout << "\033[1;36m[";
    for (int i = 0; i < n; i++)
        std::cout << keys[i] << (i == n - 1 ? "" : ", ");
    std::cout << "]\033[0m\n";

    std::string next = indent + (last ? "    " : "│   ");
    if (!leaf) {
        for (int i = 0; i <= n; i++) {
            if (C[i])
                C[i]->printHelper(next, i == n);
        }
    }
}

void BTree::printTree() {
    if (!root) {
        std::cout << " (empty)\n";
        return;
    }
    root->printHelper("", true);
}
