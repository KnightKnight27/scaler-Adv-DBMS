#include "RedBlackTree.h"
#include <iostream>

using namespace std;

Node::Node(int val) : data(val), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}

RedBlackTree::RedBlackTree() : root(nullptr) {}

RedBlackTree::~RedBlackTree() {
    destroyTree(root);
}

void RedBlackTree::destroyTree(Node* node) {
    if (node) {
        destroyTree(node->left);
        destroyTree(node->right);
        delete node;
    }
}

void RedBlackTree::leftRotate(Node* x) {
    Node* y = x->right;
    x->right = y->left;
    if (y->left != nullptr) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == nullptr) {
        root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

void RedBlackTree::rightRotate(Node* y) {
    Node* x = y->left;
    y->left = x->right;
    if (x->right != nullptr) {
        x->right->parent = y;
    }
    x->parent = y->parent;
    if (y->parent == nullptr) {
        root = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    x->right = y;
    y->parent = x;
}

void RedBlackTree::fixInsert(Node* k) {
    while (k->parent != nullptr && k->parent->color == RED) {
        if (k->parent == k->parent->parent->right) {
            Node* u = k->parent->parent->left; // uncle
            if (u != nullptr && u->color == RED) {
                u->color = BLACK;
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                k = k->parent->parent;
            } else {
                if (k == k->parent->left) {
                    k = k->parent;
                    rightRotate(k);
                }
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                leftRotate(k->parent->parent);
            }
        } else {
            Node* u = k->parent->parent->right; // uncle
            if (u != nullptr && u->color == RED) {
                u->color = BLACK;
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                k = k->parent->parent;
            } else {
                if (k == k->parent->right) {
                    k = k->parent;
                    leftRotate(k);
                }
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                rightRotate(k->parent->parent);
            }
        }
        if (k == root) break;
    }
    root->color = BLACK;
}

void RedBlackTree::insert(int key) {
    Node* node = new Node(key);
    Node* y = nullptr;
    Node* x = root;

    while (x != nullptr) {
        y = x;
        if (node->data < x->data) {
            x = x->left;
        } else {
            x = x->right;
        }
    }

    node->parent = y;
    if (y == nullptr) {
        root = node;
    } else if (node->data < y->data) {
        y->left = node;
    } else {
        y->right = node;
    }

    if (node->parent == nullptr) {
        node->color = BLACK;
        return;
    }

    if (node->parent->parent == nullptr) {
        return;
    }

    fixInsert(node);
}

void RedBlackTree::inorderHelper(Node* root) {
    if (root == nullptr) return;
    inorderHelper(root->left);
    cout << root->data << (root->color == RED ? "(R) " : "(B) ");
    inorderHelper(root->right);
}

void RedBlackTree::printInOrder() {
    inorderHelper(root);
    cout << endl;
}

bool RedBlackTree::search(int key) {
    Node* current = root;
    while (current != nullptr) {
        if (key == current->data) {
            return true;
        } else if (key < current->data) {
            current = current->left;
        } else {
            current = current->right;
        }
    }
    return false;
}
