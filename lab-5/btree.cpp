#include "btree.hpp"
#include <iostream>
#include <vector>

BTreeNode::BTreeNode(int _t, bool _leaf) {
    t = _t;
    leaf = _leaf;
    keys = new int[2 * t - 1];
    C = new BTreeNode*[2 * t];
    n = 0;
    for (int i = 0; i < 2 * t; i++) {
        C[i] = nullptr;
    }
}

BTreeNode::~BTreeNode() {
    delete[] keys;
    delete[] C;
}

// Traverse all nodes in a subtree rooted with this node
void BTreeNode::traverse() {
    int i;
    for (i = 0; i < n; i++) {
        if (!leaf) {
            C[i]->traverse();
        }
        std::cout << " " << keys[i];
    }
    if (!leaf) {
        C[i]->traverse();
    }
}

// Search key in subtree rooted with this node
BTreeNode* BTreeNode::search(int k) {
    int i = 0;
    while (i < n && k > keys[i]) {
        i++;
    }
    if (i < n && keys[i] == k) {
        return this;
    }
    if (leaf) {
        return nullptr;
    }
    return C[i]->search(k);
}

// Splitting the child y of this node.
// i is the index of y in parent's child array C. y must be full when called.
void BTreeNode::splitChild(int i, BTreeNode *y) {
    // Create a new node z which is going to store (t-1) keys of y
    BTreeNode *z = new BTreeNode(y->t, y->leaf);
    z->n = t - 1;

    // Copy the last (t-1) keys of y to z
    for (int j = 0; j < t - 1; j++) {
        z->keys[j] = y->keys[j + t];
    }

    // Copy the last t children of y to z
    if (!y->leaf) {
        for (int j = 0; j < t; j++) {
            z->C[j] = y->C[j + t];
            y->C[j + t] = nullptr;
        }
    }

    // Reduce the number of keys in y
    y->n = t - 1;

    // Since this node is going to have a new child,
    // create space of new child
    for (int j = n; j >= i + 1; j--) {
        C[j + 1] = C[j];
    }

    // Link the new child to this node
    C[i + 1] = z;

    // A key of y will move to this node. Find the location of
    // new key and move all greater keys one space ahead
    for (int j = n - 1; j >= i; j--) {
        keys[j + 1] = keys[j];
    }

    // Copy the middle key of y to this node
    keys[i] = y->keys[t - 1];

    // Increment number of keys in this node
    n = n + 1;
}

// Insert a new key in the subtree rooted with this node (must be non-full when called)
void BTreeNode::insertNonFull(int k) {
    int i = n - 1;

    if (leaf) {
        // =====================================================================
        // [ASSIGNMENT SPECIFIC TASK: INSERTION KEY POSITION SEARCH & SHIFTING]
        // This scan is similar to the search algorithm. Instead of checking for
        // exact matching, we identify the exact index position where the new key 'k'
        // should reside. Greater keys are shifted to the right to maintain in-order
        // sorted alignment of the cell keys.
        // =====================================================================
        while (i >= 0 && keys[i] > k) {
            keys[i + 1] = keys[i]; // Shift keys to the right
            i--;
        }

        // Insert the key in the correct, sorted slot
        keys[i + 1] = k;
        n = n + 1;
    } else {
        // =====================================================================
        // [ASSIGNMENT SPECIFIC TASK: DESCENDING CHILD SELECTION]
        // Find the index of the child subtree C[i] which should receive key 'k'.
        // We traverse the keys from right to left to find the correct interval.
        // =====================================================================
        while (i >= 0 && keys[i] > k) {
            i--;
        }
        i++; // The child at index i+1 (offset by current i value) is the target

        // If the selected child is full, split it
        if (C[i]->n == 2 * t - 1) {
            splitChild(i, C[i]);

            // After split, the middle key of C[i] rises to this node,
            // and C[i] is split into two halves. We inspect which of the
            // split halves will receive the new insertion.
            if (keys[i] < k) {
                i++;
            }
        }
        C[i]->insertNonFull(k);
    }
}

void BTree::insert(int k) {
    // If tree is empty
    if (root == nullptr) {
        root = new BTreeNode(t, true);
        root->keys[0] = k;
        root->n = 1;
    } else {
        // If root is full, then tree grows in height
        if (root->n == 2 * t - 1) {
            // Allocate memory for new root
            BTreeNode *s = new BTreeNode(t, false);

            // Make old root as child of new root
            s->C[0] = root;

            // Split the old root and move 1 key to the new root
            s->splitChild(0, root);

            // New root has two children now. Decide which of the
            // two children is going to have new key
            int i = 0;
            if (s->keys[0] < k) {
                i++;
            }
            s->C[i]->insertNonFull(k);

            // Change root
            root = s;
        } else {
            // If root is not full, call insertNonFull for root
            root->insertNonFull(k);
        }
    }
}

BTree::~BTree() {
    if (root != nullptr) {
        deleteTree(root);
    }
}

void BTree::deleteTree(BTreeNode* node) {
    if (node != nullptr) {
        if (!node->leaf) {
            for (int i = 0; i <= node->n; i++) {
                deleteTree(node->C[i]);
            }
        }
        delete node;
    }
}

void BTree::printTree() {
    if (root == nullptr) {
        std::cout << " (Empty B-Tree)\n";
    } else {
        root->printTreeHelper("", true);
    }
}

void BTreeNode::printTreeHelper(const std::string& indent, bool last) {
    std::cout << indent;
    if (last) {
        std::cout << "\033[1;30m└── \033[0m";
    } else {
        std::cout << "\033[1;30m├── \033[0m";
    }

    std::cout << "\033[1;36m[";
    for (int i = 0; i < n; i++) {
        std::cout << keys[i] << (i == n - 1 ? "" : ", ");
    }
    std::cout << "]\033[0m\n";

    std::string nextIndent = indent + (last ? "    " : "│   ");
    if (!leaf) {
        for (int i = 0; i <= n; i++) {
            if (C[i] != nullptr) {
                C[i]->printTreeHelper(nextIndent, i == n);
            }
        }
    }
}