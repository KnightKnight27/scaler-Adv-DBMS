// B-Tree implementation (CLRS based)

#include "BTree.h"

BTree::Node::Node(bool isLeaf) : n(0), leaf(isLeaf) {
    // init children to null
    for (int i = 0; i < 2 * DEGREE; i++) {
        children[i] = nullptr;
    }
}

BTree::BTree() {
    root = new Node(true);
}

BTree::~BTree() {
    cleanup(root);
}

void BTree::cleanup(Node *node) {
    if (node == nullptr) return;
    if (!node->leaf) {
        for (int i = 0; i <= node->n; i++) {
            cleanup(node->children[i]);
        }
    }
    delete node;
}

// INSERTION

void BTree::insert(int key) {
    Node *r = root;
    if (r->n == 2 * DEGREE - 1) {
        // root is full, grow the tree
        Node *s = new Node(false);
        root = s;
        s->children[0] = r;
        splitChild(s, 0, r);
        insertNonFull(s, key);
    } else {
        insertNonFull(r, key);
    }
}

// split a full child at index i of parent
void BTree::splitChild(Node *parent, int i, Node *child) {
    Node *z = new Node(child->leaf);
    z->n = DEGREE - 1;

    // copy the right half of child's keys to z
    for (int j = 0; j < DEGREE - 1; j++) {
        z->keys[j] = child->keys[j + DEGREE];
    }

    // copy children too if internal node
    if (!child->leaf) {
        for (int j = 0; j < DEGREE; j++) {
            z->children[j] = child->children[j + DEGREE];
        }
    }

    child->n = DEGREE - 1;

    // insert z into parent's children
    for (int j = parent->n; j >= i + 1; j--) {
        parent->children[j + 1] = parent->children[j];
    }
    parent->children[i + 1] = z;

    // move the median key up to parent
    for (int j = parent->n - 1; j >= i; j--) {
        parent->keys[j + 1] = parent->keys[j];
    }
    parent->keys[i] = child->keys[DEGREE - 1];
    parent->n++;
}

// insert key into a node that is not full
void BTree::insertNonFull(Node *node, int key) {
    int i = node->n - 1;

    if (node->leaf) {
        // shift keys right to make room
        while (i >= 0 && key < node->keys[i]) {
            node->keys[i + 1] = node->keys[i];
            i--;
        }
        node->keys[i + 1] = key;
        node->n++;
    } else {
        // find child to descend into
        while (i >= 0 && key < node->keys[i]) {
            i--;
        }
        i++;

        // split if child is full
        if (node->children[i]->n == 2 * DEGREE - 1) {
            splitChild(node, i, node->children[i]);
            if (key > node->keys[i]) {
                i++;
            }
        }
        insertNonFull(node->children[i], key);
    }
}

// SEARCH

bool BTree::find(int key) {
    return findHelper(root, key);
}

bool BTree::findHelper(Node *node, int key) {
    if (node == nullptr) return false;

    int i = 0;
    while (i < node->n && key > node->keys[i]) {
        i++;
    }

    if (i < node->n && key == node->keys[i]) {
        return true;
    }

    if (node->leaf) {
        return false;
    }

    return findHelper(node->children[i], key);
}

// PRINT (sideway tree, root on left)

void BTree::traverse(Node *node, int depth) {
    if (node == nullptr) return;

    for (int i = node->n - 1; i >= 0; i--) {
        if (!node->leaf) {
            traverse(node->children[i + 1], depth + 1);
        }
        for (int d = 0; d < depth; d++) {
            std::cout << "    ";
        }
        std::cout << node->keys[i] << "\n";
    }
    if (!node->leaf) {
        traverse(node->children[0], depth + 1);
    }
}

void BTree::print() {
    traverse(root, 0);
}
