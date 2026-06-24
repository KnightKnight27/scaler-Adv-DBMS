#include "btree.hpp"
#include <iostream>
#include <queue>

using namespace std;

// ─── BTreeNode ────────────────────────────────────────────────────────────────

BTreeNode::BTreeNode(bool isLeaf) {
    leaf = isLeaf;
    n    = 0;
    for (int i = 0; i < 2 * T; i++) children[i] = nullptr;
}

// In-order traversal
void BTreeNode::traverse() {
    int i;
    for (i = 0; i < n; i++) {
        if (!leaf)
            children[i]->traverse();
        cout << keys[i] << " ";
    }
    // Visit last child
    if (!leaf)
        children[i]->traverse();
}

// Search in subtree rooted here
BTreeNode* BTreeNode::search(int key) {
    int i = 0;
    while (i < n && key > keys[i]) i++;

    if (i < n && keys[i] == key)
        return this;

    if (leaf)
        return nullptr;

    return children[i]->search(key);
}

// Split child y (full) at position i
void BTreeNode::splitChild(int i, BTreeNode* y) {
    // New node will hold the second half of y's keys
    BTreeNode* z = new BTreeNode(y->leaf);
    z->n = T - 1;

    // Copy last (T-1) keys of y into z
    for (int j = 0; j < T - 1; j++)
        z->keys[j] = y->keys[j + T];

    // Copy last T children of y into z (if not leaf)
    if (!y->leaf) {
        for (int j = 0; j < T; j++)
            z->children[j] = y->children[j + T];
    }

    // Reduce key count of y
    y->n = T - 1;

    // Shift children of this node to make room for z
    for (int j = n; j >= i + 1; j--)
        children[j + 1] = children[j];
    children[i + 1] = z;

    // Shift keys of this node to find location for middle key
    for (int j = n - 1; j >= i; j--)
        keys[j + 1] = keys[j];

    // Copy middle key of y up into this node
    keys[i] = y->keys[T - 1];
    n++;
}

// Insert into a non-full node
void BTreeNode::insertNonFull(int key) {
    int i = n - 1;

    if (leaf) {
        // Shift keys right to find insertion point
        while (i >= 0 && keys[i] > key) {
            keys[i + 1] = keys[i];
            i--;
        }
        keys[i + 1] = key;
        n++;
    } else {
        // Find which child to descend into
        while (i >= 0 && keys[i] > key) i--;
        i++;

        if (children[i]->n == 2 * T - 1) {
            // Child is full — split it first
            splitChild(i, children[i]);
            if (keys[i] < key) i++;
        }
        children[i]->insertNonFull(key);
    }
}

// ─── BTree ────────────────────────────────────────────────────────────────────

BTree::BTree() {
    root = new BTreeNode(true);
}

void BTree::traverse() {
    cout << "B-Tree In-Order Traversal: ";
    if (root) root->traverse();
    cout << endl;
}

BTreeNode* BTree::search(int key) {
    return root ? root->search(key) : nullptr;
}

void BTree::insert(int key) {
    BTreeNode* r = root;

    if (r->n == 2 * T - 1) {
        // Root is full — grow the tree upward
        BTreeNode* s = new BTreeNode(false);
        root          = s;
        s->children[0] = r;
        s->splitChild(0, r);

        // Decide which child to insert into
        int i = (s->keys[0] < key) ? 1 : 0;
        s->children[i]->insertNonFull(key);
    } else {
        r->insertNonFull(key);
    }

    cout << "Inserted " << key << " into B-Tree\n";
}

void BTree::printLevel(BTreeNode* node, int level) {
    if (!node) return;

    cout << "Level " << level << ": [";
    for (int i = 0; i < node->n; i++) {
        cout << node->keys[i];
        if (i < node->n - 1) cout << ", ";
    }
    cout << "]\n";

    if (!node->leaf) {
        for (int i = 0; i <= node->n; i++)
            printLevel(node->children[i], level + 1);
    }
}

void BTree::printTree() {
    cout << "\n=== B-Tree Structure ===\n";
    printLevel(root, 0);
    cout << "========================\n";
}