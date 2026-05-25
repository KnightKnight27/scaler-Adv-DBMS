#include "RedBlackTree.h"
#include <iostream>
#include <queue>
#include <string>
#include <vector>

/*
 * insert fixup cases:
 *
 * case 0 - parent is black -> no violation, done
 * case 1 - parent is red, uncle is red -> recolor parent+uncle black, grandparent red, recurse up
 * case 2 - parent is red, uncle is black, node is on opposite side from parent (zig-zag)
 *          -> rotate parent to convert to case 3
 * case 3 - parent is red, uncle is black, node is on same side as parent (zig-zig)
 *          -> rotate grandparent, recolor
 */

RedBlackTree::RedBlackTree() : NIL(new Node(0)) {
    NIL->color = Color::black;
    m_Root = NIL;
}

RedBlackTree::~RedBlackTree() {
    destroyTree(m_Root);
    delete NIL;
}

void RedBlackTree::destroyTree(Node *node) {
    if (node == NIL) return;
    destroyTree(node->left);
    destroyTree(node->right);
    delete node;
}

bool RedBlackTree::find(int val) {
    Node *node = m_Root;
    while (node != NIL) {
        if (val == node->val)      return true;
        else if (val < node->val)  node = node->left;
        else                       node = node->right;
    }
    return false;
}

void RedBlackTree::insert(int val) {
    Node *parent = nullptr;
    Node *trav = m_Root;
    bool isLeftChild = false;

    while (trav != NIL) {
        parent = trav;
        if (val <= trav->val) { trav = trav->left;  isLeftChild = true;  }
        else                  { trav = trav->right; isLeftChild = false; }
    }

    Node *node = new Node(val);
    node->left = NIL;
    node->right = NIL;

    if (parent == nullptr) {
        m_Root = node;
        m_Root->color = Color::black;
        return;
    }

    node->parent = parent;
    if (isLeftChild) parent->left  = node;
    else             parent->right = node;

    fixTree(node);
}

void RedBlackTree::remove(int val) {
    // TODO
}

// -- rotations --

void RedBlackTree::rotateLeft(Node *x) {
    Node *y = x->right;
    x->right = y->left;
    if (y->left != NIL) y->left->parent = x;

    y->parent = x->parent;
    if (x->parent == nullptr)       m_Root = y;
    else if (x == x->parent->left)  x->parent->left  = y;
    else                            x->parent->right = y;

    y->left = x;
    x->parent = y;
}

void RedBlackTree::rotateRight(Node *y) {
    Node *x = y->left;
    y->left = x->right;
    if (x->right != NIL) x->right->parent = y;

    x->parent = y->parent;
    if (y->parent == nullptr)       m_Root = x;
    else if (y == y->parent->left)  y->parent->left  = x;
    else                            y->parent->right = x;

    x->right = y;
    y->parent = x;
}

// -- fix-up dispatch --

void RedBlackTree::fixTree(Node *node) {
    if      (isCase0(node)) handleCase0(node);
    else if (isCase3(node)) handleCase3(node);
    else if (isCase1(node)) handleCase1(node);
    else if (isCase2(node)) handleCase2(node);
}

// -- case checks --

bool RedBlackTree::isCase0(Node *node) {
    return node->parent && node->parent->color == Color::black;
}

bool RedBlackTree::isCase1(Node *node) {
    Node *uncle = getUncle(node);
    return node->parent && node->parent->color == Color::red
        && uncle && uncle->color == Color::red;
}

bool RedBlackTree::isCase2(Node *node) {
    Node *uncle = getUncle(node);
    return node->parent && node->parent->color == Color::red
        && uncle && uncle->color == Color::black;
}

bool RedBlackTree::isCase3(Node *node) {
    Node *parent = node->parent;
    Node *grandparent = getGrandParent(node);
    if (!parent || grandparent == NIL) return false;

    bool leftZigZig  = (node == parent->left  && parent == grandparent->left  && parent->color == Color::red);
    bool rightZigZig = (node == parent->right && parent == grandparent->right && parent->color == Color::red);
    return leftZigZig || rightZigZig;
}

// -- case handlers --

void RedBlackTree::handleCase0(Node *node) {
    // parent is black, nothing to fix
    (void)node;
}

void RedBlackTree::handleCase1(Node *node) {
    Node *parent = node->parent;
    Node *uncle = getUncle(node);
    Node *grandparent = getGrandParent(node);

    parent->color = Color::black;
    uncle->color  = Color::black;

    if (grandparent == m_Root) {
        grandparent->color = Color::black;
    } else {
        grandparent->color = Color::red;
        fixTree(grandparent);
    }
}

void RedBlackTree::handleCase2(Node *node) {
    // zig-zag: rotate parent to turn into zig-zig, then apply case 3
    Node *parent = node->parent;
    Node *grandparent = getGrandParent(node);

    if (node == parent->right && grandparent->left == parent) {
        rotateLeft(parent);
        handleCase3(parent);   // original parent is now in zig-zig position
    } else {
        rotateRight(parent);
        handleCase3(parent);
    }
}

void RedBlackTree::handleCase3(Node *node) {
    // zig-zig: rotate grandparent and recolor
    Node *parent = node->parent;
    Node *grandparent = getGrandParent(node);

    parent->color      = Color::black;
    grandparent->color = Color::red;

    if (node == parent->left) rotateRight(grandparent);
    else                      rotateLeft(grandparent);
}

// -- helpers --

RedBlackTree::Node* RedBlackTree::getGrandParent(Node *node) {
    if (node->parent && node->parent->parent)
        return node->parent->parent;
    return NIL;
}

RedBlackTree::Node* RedBlackTree::getUncle(Node *node) {
    Node *gp = getGrandParent(node);
    if (!node->parent || gp == NIL) return NIL;
    return (gp->left == node->parent) ? gp->right : gp->left;
}

// -- print (BFS, LeetCode style) --

void RedBlackTree::print() {
    if (m_Root == NIL) { std::cout << "[]\n"; return; }

    std::vector<std::string> result;
    std::queue<Node*> q;
    q.push(m_Root);

    while (!q.empty()) {
        Node *node = q.front(); q.pop();
        if (node == NIL) {
            result.emplace_back("null");
        } else {
            result.emplace_back(std::to_string(node->val) + (node->color == Color::red ? "(R)" : "(B)"));
            q.push(node->left);
            q.push(node->right);
        }
    }

    while (!result.empty() && result.back() == "null") result.pop_back();

    std::cout << "[";
    for (size_t i = 0; i < result.size(); i++) {
        std::cout << result[i];
        if (i + 1 < result.size()) std::cout << ", ";
    }
    std::cout << "]\n";
}
