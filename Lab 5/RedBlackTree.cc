#include "RedBlackTree.h"

RBTree::RBTree() { root = nullptr; }

void RBTree::rotateLeft(RBNode* x) {
    RBNode* y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    y->parent = x->parent;
    if (!x->parent) root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}

void RBTree::rotateRight(RBNode* x) {
    RBNode* y = x->left;
    x->left = y->right;
    if (y->right) y->right->parent = x;
    y->parent = x->parent;
    if (!x->parent) root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;
    y->right = x;
    x->parent = y;
}

void RBTree::insertFixup(RBNode* z) {
    while (z->parent && z->parent->color == RED) {
        RBNode* gp = z->parent->parent;
        if (!gp) break;
        if (z->parent == gp->left) {
            RBNode* y = gp->right;
            if (y && y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                gp->color = RED;
                z = gp;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    rotateLeft(z);
                }
                z->parent->color = BLACK;
                gp->color = RED;
                rotateRight(gp);
            }
        } else {
            RBNode* y = gp->left;
            if (y && y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                gp->color = RED;
                z = gp;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rotateRight(z);
                }
                z->parent->color = BLACK;
                gp->color = RED;
                rotateLeft(gp);
            }
        }
    }
    if (root) root->color = BLACK;
}

void RBTree::insert(int key) {
    RBNode* z = new RBNode(key);
    RBNode* y = nullptr;
    RBNode* x = root;
    while (x) {
        y = x;
        if (z->key < x->key) x = x->left;
        else x = x->right;
    }
    z->parent = y;
    if (!y) root = z;
    else if (z->key < y->key) y->left = z;
    else y->right = z;

    insertFixup(z);
}

void RBTree::inorder(RBNode* node, std::ostream &out) {
    if (!node) return;
    inorder(node->left, out);
    out << node->key << (node->color == RED ? "(R)" : "(B)") << " ";
    inorder(node->right, out);
}

void RBTree::inorderPrint(std::ostream &out) {
    inorder(root, out);
}
