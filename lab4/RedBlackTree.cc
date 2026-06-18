/*
 * Written by Ojas Maheshwari
 * CC 2026 - Present
 *
 * Theory
 * 
 * Case 0 - Parent of node is BLACK. No collisions or black height imbalance so direct insert
 * Case 1 - Parent is RED and uncle is RED. Re-color parent and uncle to be BLACK and grandparent to be RED. Continue fixing with grandparent.
 * Case 2 - Parent is RED and uncle is BLACK and inner node configuration:
 *      2.1 - LR configuration. Perform left rotation at P and use Case 3.
 *      2.2 - RL configuration. Perform right rotation at P and use Case 3.
 * Case 3 - Parent is RED and uncle is BLACK and outer node configuration:
 *      3.1 - LL configuration. Perform right rotation at grandparent and re-color parent to BLACK and grandparent to RED.
 *      3.2 - RR configuration. Perform left rotation at grandparent anr re-color parent to BLACK and grandparent to RED.
*/

#include "RedBlackTree.h"
#include <cassert>
#include <vector>
#include <queue>

RedBlackTree::RedBlackTree()
    : m_Root(nullptr)
{
}

RedBlackTree::~RedBlackTree()
{
    delete m_Root;
}


bool RedBlackTree::find(int val) {
    Node *trav = m_Root;

    while (trav != nullptr) {
        if (trav->val == val) {
            return true;
        } else if (val < trav->val) {
            trav = trav->left;
        } else {
            trav = trav->right;
        }
    }

    return false;
}

void RedBlackTree::insert(int val)
{
    Node *node = new Node(val);

    if (m_Root == nullptr) {
        m_Root = node;
        m_Root->color = BLACK;
        return;
    }

    Node *trav = m_Root;
    Node *parent = m_Root;
    bool lastMoveLeft = false;

    while (trav != nullptr) {
        parent = trav;

        if (val <= trav->val) {
            trav = trav->left;
            lastMoveLeft = true;
        } else {
            trav = trav->right;
            lastMoveLeft = false;
        }
    }

    if (lastMoveLeft) {
        parent->left = node;
    } else {
        parent->right = node;
    }
    node->parent = parent;

    fixTree(node);
}

RedBlackTree::Node* RedBlackTree::getGrandParent(Node *node)
{
    if (node && node->parent) {
        return node->parent->parent;
    } else {
        return nullptr;
    }
}

RedBlackTree::Node* RedBlackTree::getUncle(Node *node)
{
    Node *grandparent = getGrandParent(node);
    if (!node || !grandparent) {
        return nullptr;
    }
    return (grandparent->left == node->parent) ? grandparent->right : grandparent->left;
}

void RedBlackTree::leftRotate(Node *node)
{
    assert(node && node->right != nullptr);

    Node *X = node;
    Node *P = X->parent;
    bool isXLeftChildOfP = false;
    if (P) {
        isXLeftChildOfP = (P->left == X);
    }
    Node *Y = X->right;
    Node *Z = X->left;
    Node *M = Y->left;
    Node *N = Y->right;

    Y->parent = nullptr;
    Y->left = X;
    X->parent = Y;
    X->left = Z;
    X->right = M;
    if (Z) {
        Z->parent = X;
    }
    if (M) {
        M->parent = X;
    }
    Y->right = N;
    if (N) {
        N->parent = Y;
    }

    Y->parent = P;
    if (P) {
        if (isXLeftChildOfP) {
            P->left = Y;
        } else {
            P->right = Y;
        }
    } else {
        m_Root = Y;
    }
}

void RedBlackTree::rightRotate(Node *node)
{
    assert(node && node->left != nullptr);

    Node *X = node;
    Node *P = X->parent;
    bool isXLeftChildOfP = false;
    if (P) {
        isXLeftChildOfP = (P->left == X);
    }
    Node *Y = X->left;
    Node *Z = X->right;
    Node *M = Y->left;
    Node *N = Y->right;

    Y->right = X;
    X->parent = Y;
    Y->left = M;
    if (M) {
        M->parent = Y;
    }
    X->left = N;
    if (N) {
        N->parent = X;
    }
    X->right = Z;
    if (Z) {
        Z->parent = X;
    }

    Y->parent = P;
    if (P) {
        if (isXLeftChildOfP) {
            P->left = Y;
        } else {
            P->right = Y;
        }
    } else {
        m_Root = Y;
    }
}

bool RedBlackTree::isLRConfiguration(Node *node)
{
    assert(node && node->parent && node->parent->parent);

    Node *grandparent = getGrandParent(node);
    Node *parent = node->parent;

    return (grandparent->left == parent && parent->right == node);
}

bool RedBlackTree::isRLConfiguration(Node *node)
{
    assert(node && node->parent && node->parent->parent);

    Node *grandparent = getGrandParent(node);
    Node *parent = node->parent;

    return (grandparent->right == parent && parent->left == node);
}

bool RedBlackTree::isLLConfiguration(Node *node)
{
    assert(node && node->parent && node->parent->parent);

    Node *grandparent = getGrandParent(node);
    Node *parent = node->parent;

    return (grandparent->left == parent && parent->left == node);
}

bool RedBlackTree::isRRConfiguration(Node *node)
{
    assert(node && node->parent && node->parent->parent);

    Node *grandparent = getGrandParent(node);
    Node *parent = node->parent;

    return (grandparent->right == parent && parent->right == node);
}

void RedBlackTree::fixTree(Node *node)
{
    Node *parent = node->parent;
    Node *grandparent = getGrandParent(node);

    if (!parent) {
        node->color = BLACK;
        return;
    }
    
    if (parent->color == BLACK) {
        // Case 0
        return;
    }

    Node *uncle = getUncle(node);

    if (uncle && uncle->color == RED) {
        // Case 1
        parent->color = BLACK;
        uncle->color = BLACK;
        grandparent->color = RED;

        fixTree(grandparent);
    } else {
        if (isLRConfiguration(node)) {
            // Case 2.1
            leftRotate(parent);
            fixTree(parent);
        } else if (isRLConfiguration(node)) {
            // Case 2.2
            rightRotate(parent);
            fixTree(parent);
        } else if (isLLConfiguration(node)) {
            // Case 3.1
            rightRotate(grandparent);
            parent->color = BLACK;
            grandparent->color = RED;
        } else {
            // Case 3.2
            leftRotate(grandparent);
            parent->color = BLACK;
            grandparent->color = RED;
        }
    }
}

void RedBlackTree::print()
{
    if (m_Root == nullptr) {
        std::cout << "[]\n";
        return;
    }

    std::vector<std::string> result;
    std::vector<Node*> nodes;
    std::queue<Node*> q;

    q.push(m_Root);

    while (!q.empty()) {
        Node* node = q.front();
        q.pop();

        if (node == nullptr) {
            result.emplace_back("null");
        } else {
            result.emplace_back(std::to_string(node->val));
            nodes.push_back(node);

            q.push(node->left);
            q.push(node->right);
        }
    }

    while (!result.empty() && result.back() == "null") {
        result.pop_back();
    }

    std::cout << "[";

    for (size_t i = 0; i < result.size(); ++i) {
        std::cout << result[i];

        if (i + 1 < result.size()) {
            std::cout << ", ";
        }
    }

    std::cout << "]\n\n";

    std::cout << "Colors:\n";

    for (Node* node : nodes) {
        std::cout
            << node->val
            << " = "
            << (node->color == RED ? "RED" : "BLACK")
            << '\n';
    }
}
