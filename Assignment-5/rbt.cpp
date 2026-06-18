// rbt.cpp — Lab 5
// Tanishq Singh | 24BCS10303
//
// Red-Black Tree. Standard textbook implementation (CLRS style).
//
// Rules this enforces:
//   1. Every node is red or black.
//   2. Root is always black.
//   3. No two consecutive red nodes on any path.
//   4. Every path from root to a NIL leaf has the same black-node count.
//
// The sentinel NIL node makes the edge cases cleaner — we never have to
// null-check before reading a color, and rotations don't need special handling
// for missing children.

#include "rbt.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

RedBlackTree::RedBlackTree() {
    NIL = new RBTNode(0, BLACK);
    NIL->left = NIL->right = NIL->parent = NIL;
    root = NIL;
}

RedBlackTree::~RedBlackTree() {
    clear(root);
    delete NIL;
}

void RedBlackTree::clear(RBTNode* node) {
    if (node != NIL && node != nullptr) {
        clear(node->left);
        clear(node->right);
        delete node;
    }
}

RBTNode* RedBlackTree::search(int key) {
    RBTNode* cur = root;
    while (cur != NIL && key != cur->key)
        cur = (key < cur->key) ? cur->left : cur->right;
    return (cur == NIL) ? nullptr : cur;
}

// left rotation: pivot x upward, x->right becomes the new subtree root
void RedBlackTree::leftRotate(RBTNode* x) {
    RBTNode* y = x->right;
    x->right = y->left;
    if (y->left != NIL)
        y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL)
        root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left = x;
    x->parent = y;
}

// right rotation: symmetric to leftRotate
void RedBlackTree::rightRotate(RBTNode* y) {
    RBTNode* x = y->left;
    y->left = x->right;
    if (x->right != NIL)
        x->right->parent = y;
    x->parent = y->parent;
    if (y->parent == NIL)
        root = x;
    else if (y == y->parent->right)
        y->parent->right = x;
    else
        y->parent->left = x;
    x->right = y;
    y->parent = x;
}

void RedBlackTree::insert(int key) {
    RBTNode* z = new RBTNode(key, RED);
    z->left = z->right = z->parent = NIL;

    RBTNode* parent = NIL;
    RBTNode* cur = root;
    while (cur != NIL) {
        parent = cur;
        cur = (z->key < cur->key) ? cur->left : cur->right;
    }

    z->parent = parent;
    if (parent == NIL)
        root = z;
    else if (z->key < parent->key)
        parent->left = z;
    else
        parent->right = z;

    fixup(z);
}

// fixup — fixes red-red violations introduced by inserting z as RED.
// Three cases (mirrored for left/right):
//   Case 1: uncle is red   → recolor, push problem up
//   Case 2: uncle is black, z is inner child → rotate to make it outer
//   Case 3: uncle is black, z is outer child → rotate + recolor, done
void RedBlackTree::fixup(RBTNode* z) {
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            RBTNode* uncle = z->parent->parent->right;
            if (uncle->color == RED) {
                // case 1
                z->parent->color = BLACK;
                uncle->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    // case 2 → convert to case 3
                    z = z->parent;
                    leftRotate(z);
                }
                // case 3
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rightRotate(z->parent->parent);
            }
        } else {
            RBTNode* uncle = z->parent->parent->left;
            if (uncle->color == RED) {
                // case 1 (mirrored)
                z->parent->color = BLACK;
                uncle->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    // case 2 (mirrored)
                    z = z->parent;
                    rightRotate(z);
                }
                // case 3 (mirrored)
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                leftRotate(z->parent->parent);
            }
        }
    }
    root->color = BLACK;
}

void RedBlackTree::printTree() {
    if (root == NIL)
        std::cout << " (empty)\n";
    else
        printRec(root, "", true);
}

void RedBlackTree::printRec(RBTNode* node, const std::string& indent, bool last) {
    if (node == NIL) return;

    std::cout << indent;
    std::cout << (last ? "\033[1;30m└── \033[0m" : "\033[1;30m├── \033[0m");

    const char* col = (node->color == RED) ? "\033[1;31m" : "\033[1;37m";
    const char* label = (node->color == RED) ? "[R]" : "[B]";
    std::cout << col << node->key << " " << label << "\033[0m\n";

    std::string next = indent + (last ? "    " : "│   ");
    bool hasLeft  = node->left  != NIL;
    bool hasRight = node->right != NIL;
    if (hasLeft || hasRight) {
        printRec(node->right, next, false);
        printRec(node->left,  next, true);
    }
}

int RedBlackTree::getHeight() { return height(root); }

int RedBlackTree::height(RBTNode* node) {
    if (node == NIL || node == nullptr) return 0;
    return 1 + std::max(height(node->left), height(node->right));
}

int RedBlackTree::getBlackHeight() { return blackHeight(root); }

int RedBlackTree::blackHeight(RBTNode* node) {
    int bh = 0;
    RBTNode* cur = node;
    while (cur != NIL) {
        if (cur->color == BLACK) bh++;
        cur = cur->left;
    }
    return bh;
}

bool RedBlackTree::isRBBalanced() {
    if (root == NIL) return true;
    if (root->color != BLACK) return false;
    int target = -1;
    return checkRB(root, 0, target);
}

bool RedBlackTree::checkRB(RBTNode* node, int bh, int& target) {
    if (node == NIL) {
        if (target == -1) { target = bh; return true; }
        return bh == target;
    }
    if (node->color == RED &&
        (node->left->color == RED || node->right->color == RED))
        return false;
    int next = bh + (node->color == BLACK ? 1 : 0);
    return checkRB(node->left, next, target) && checkRB(node->right, next, target);
}

bool RedBlackTree::isAVLBalanced() {
    auto check = [&](auto& self, RBTNode* node) -> bool {
        if (node == NIL) return true;
        int lh = height(node->left);
        int rh = height(node->right);
        if (std::abs(lh - rh) > 1) return false;
        return self(self, node->left) && self(self, node->right);
    };
    return check(check, root);
}

std::vector<std::pair<int,std::pair<int,int>>> RedBlackTree::getBalanceInfo() {
    std::vector<std::pair<int,std::pair<int,int>>> result;
    collectInfo(root, result);
    return result;
}

void RedBlackTree::collectInfo(RBTNode* node, std::vector<std::pair<int,std::pair<int,int>>>& out) {
    if (node == NIL) return;
    collectInfo(node->left, out);
    int lh = height(node->left);
    int rh = height(node->right);
    out.push_back({node->key, {1 + std::max(lh, rh), lh - rh}});
    collectInfo(node->right, out);
}
