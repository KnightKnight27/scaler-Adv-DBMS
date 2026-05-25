#include "RedBlackTree.h"
#include <iostream>
#include <queue>
#include <string>
#include <vector>

RedBlackTree::RedBlackTree() {
    nil = new Node(0, nullptr);
    nil->color = Color::Black;
    nil->left = nil->right = nil;
    root = nil;
}

RedBlackTree::~RedBlackTree() {
    freeTree(root);
    delete nil;
}

void RedBlackTree::freeTree(Node *node) {
    if (node == nil) return;
    freeTree(node->left);
    freeTree(node->right);
    delete node;
}

bool RedBlackTree::search(int val) {
    Node *cur = root;
    while (cur != nil) {
        if      (val == cur->data) return true;
        else if (val  < cur->data) cur = cur->left;
        else                       cur = cur->right;
    }
    return false;
}

void RedBlackTree::rotateLeft(Node *x) {
    Node *y = x->right;
    x->right = y->left;
    if (y->left != nil) y->left->parent = x;

    y->parent = x->parent;
    if      (x->parent == nullptr) root = y;
    else if (x == x->parent->left) x->parent->left  = y;
    else                           x->parent->right = y;

    y->left   = x;
    x->parent = y;
}

void RedBlackTree::rotateRight(Node *x) {
    Node *y = x->left;
    x->left = y->right;
    if (y->right != nil) y->right->parent = x;

    y->parent = x->parent;
    if      (x->parent == nullptr) root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else                            x->parent->left  = y;

    y->right  = x;
    x->parent = y;
}

void RedBlackTree::insert(int val) {
    Node *node = new Node(val, nil);

    Node *parent = nullptr;
    Node *cur    = root;

    while (cur != nil) {
        parent = cur;
        cur = (val < cur->data) ? cur->left : cur->right;
    }

    node->parent = parent;

    if (parent == nullptr)       root = node;
    else if (val < parent->data) parent->left  = node;
    else                         parent->right = node;

    if (node->parent == nullptr) {
        node->color = Color::Black;
        return;
    }

    if (node->parent->parent == nullptr) return;

    fixInsert(node);
}

void RedBlackTree::fixInsert(Node *node) {
    while (node->parent != nullptr && isRed(node->parent)) {
        Node *parent      = node->parent;
        Node *grandparent = parent->parent;

        if (parent == grandparent->left) {
            Node *uncle = grandparent->right;

            if (isRed(uncle)) {
                // recolor and move up
                parent->color      = Color::Black;
                uncle->color       = Color::Black;
                grandparent->color = Color::Red;
                node = grandparent;
            } else {
                if (node == parent->right) {
                    // zig-zag: rotate parent left first
                    node = parent;
                    rotateLeft(node);
                    parent      = node->parent;
                    grandparent = parent->parent;
                }
                // zig-zig: rotate grandparent right
                parent->color      = Color::Black;
                grandparent->color = Color::Red;
                rotateRight(grandparent);
            }
        } else {
            // mirror: parent is right child
            Node *uncle = grandparent->left;

            if (isRed(uncle)) {
                parent->color      = Color::Black;
                uncle->color       = Color::Black;
                grandparent->color = Color::Red;
                node = grandparent;
            } else {
                if (node == parent->left) {
                    node = parent;
                    rotateRight(node);
                    parent      = node->parent;
                    grandparent = parent->parent;
                }
                parent->color      = Color::Black;
                grandparent->color = Color::Red;
                rotateLeft(grandparent);
            }
        }
    }
    root->color = Color::Black;
}

void RedBlackTree::print() {
    if (root == nil) { std::cout << "[]\n"; return; }

    std::vector<std::string> out;
    std::queue<Node*> q;
    q.push(root);

    while (!q.empty()) {
        Node *n = q.front(); q.pop();
        if (n == nil) {
            out.emplace_back("null");
        } else {
            out.emplace_back(std::to_string(n->data) + (n->color == Color::Red ? "(R)" : "(B)"));
            q.push(n->left);
            q.push(n->right);
        }
    }

    while (!out.empty() && out.back() == "null") out.pop_back();

    std::cout << "[";
    for (size_t i = 0; i < out.size(); i++) {
        if (i) std::cout << ", ";
        std::cout << out[i];
    }
    std::cout << "]\n";
}
