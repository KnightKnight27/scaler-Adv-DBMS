#include "RedBlackTree.h"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>
#include <queue>

#define DEBUG

#ifdef DEBUG
#define LOG(x) std::cout << x << '\n';
#else
#define LOG(x) ;
#endif

/* Theory
 *
 * "insert" node -> node to be inserted
 *
 * Case 0 - Parent of insert node is black
 * Case 1 - Parent of insert node is red and insert node is right of parent and uncle of insert node (sibling of parent) is also red
 * Case 2 - Parent of insert node is red and insert node is right of parent and uncle of insert node is black
 * Case 3 - Parent of insert node is red and insert node is left of parent
 *
 * Solutions to each case:
 *
 * Case 0 - Directly insert the node since it doesn't violate any rules
 * Case 1 - Switch colors of grandparent with parent and uncle nodes. Propagate this further with the grandparent as the grandparent might now be red and violate the "no two adjacent reds" rule
 * Case 2 - Perform a "left rotation" and transform situation to case 3
 * Case 3 - Perform a "right rotation"
 */

RedBlackTree::RedBlackTree()
    : sentinel(new Node(0))
{
    sentinel->col = Color::black;
    root_ = sentinel;
}

RedBlackTree::~RedBlackTree()
{
    deleteSubtree(root_);
    delete sentinel;
}

bool RedBlackTree::find(int val)
{
    Node *cur = root_;
    while (cur != sentinel)
    {
        if (val == cur->key)
            return true;
        cur = (val < cur->key) ? cur->leftChild : cur->rightChild;
    }
    return false;
}

void RedBlackTree::insert(int val)
{
    Node *parent = sentinel;
    Node *cur = root_;

    while (cur != sentinel)
    {
        parent = cur;
        if (val <= cur->key)
            cur = cur->leftChild;
        else
            cur = cur->rightChild;
    }

    Node *node = new Node(val);
    node->leftChild = sentinel;
    node->rightChild = sentinel;
    node->parentNode = parent;

    if (parent == sentinel)
    {
        root_ = node;
        node->col = Color::black;
    }
    else if (val <= parent->key)
    {
        parent->leftChild = node;
    }
    else
    {
        parent->rightChild = node;
    }

    fixTree(node);
}

void RedBlackTree::remove(int val)
{
    // TO BE IMPLEMENTED
}

void RedBlackTree::fixTree(Node *node)
{
    if (isCase0(node))
    {
        handleCase0(node);
    }
    else if (isCase3(node))
    {
        handleCase3(node);
    }
    else if (isCase1(node))
    {
        handleCase1(node);
    }
    else if (isCase2(node))
    {
        handleCase2(node);
    }
    else
    {
        LOG("Unknown case");
        LOG("node: " << node->key << " L " << (node->leftChild ? node->leftChild->key : -1) << " R " << (node->rightChild ? node->rightChild->key : -1) << '\n');
    }
}

bool RedBlackTree::isCase0(Node *node)
{
    return (node->parentNode && node->parentNode->col == Color::black);
}
bool RedBlackTree::isCase1(Node *node)
{
    Node *uncle = getUncle(node);

    return (node->parentNode && node->parentNode->col == Color::red && uncle && uncle->col == Color::red);
}
bool RedBlackTree::isCase2(Node *node)
{
    Node *uncle = getUncle(node);

    return (node->parentNode && node->parentNode->col == Color::red && uncle && uncle->col == Color::black);
}
bool RedBlackTree::isCase3(Node *node)
{
    Node *parent = node->parentNode;
    Node *grandparent = getGrandParent(node);

    bool leftLeaning = (parent && parent->leftChild == node && grandparent && grandparent->leftChild == parent && parent->col == Color::red);
    bool rightLeaning = (parent && parent->rightChild == node && grandparent && grandparent->rightChild == parent && parent->col == Color::red);

    return (leftLeaning || rightLeaning);
}

RedBlackTree::Node *RedBlackTree::getGrandParent(Node *node)
{
    if (node->parentNode && node->parentNode->parentNode)
    {
        return node->parentNode->parentNode;
    }
    else
    {
        return sentinel;
    }
}
RedBlackTree::Node *RedBlackTree::getUncle(Node *node)
{
    Node *grandparent = getGrandParent(node);

    if (!node->parentNode || grandparent == sentinel)
    {
        return sentinel;
    }

    bool isParentLeftOfGrandparent = (grandparent->leftChild == node->parentNode);

    if (isParentLeftOfGrandparent)
    {
        return grandparent->rightChild;
    }
    else
    {
        return grandparent->leftChild;
    }
}

void RedBlackTree::handleCase0(Node *node)
{
    LOG("case0");
    if (node->leftChild == nullptr)
        node->leftChild = sentinel;
    if (node->rightChild == nullptr)
        node->rightChild = sentinel;
}

void RedBlackTree::handleCase1(Node *node)
{
    LOG("case1");
    Node *grandparent = getGrandParent(node);
    Node *uncle = getUncle(node);
    Node *parent = node->parentNode;

    parent->col = Color::black;
    uncle->col = Color::black;
    if (grandparent == root_)
    {
        grandparent->col = Color::black;
    }
    else
    {
        grandparent->col = Color::red;
        fixTree(grandparent);
    }
}

void RedBlackTree::handleCase2(Node *node)
{
    LOG("case2");
    Node *parent = node->parentNode;
    Node *grandparent = getGrandParent(node);

    grandparent->leftChild = node;
    node->parentNode = grandparent;
    node->leftChild = parent;
    parent->parentNode = node;

    fixTree(parent);
}

void RedBlackTree::handleCase3(Node *node)
{
    LOG("case3");
    Node *parent = node->parentNode;
    Node *grandparent = getGrandParent(node);

    parent->leftChild = node;
    parent->rightChild = grandparent;
    parent->parentNode = grandparent->parentNode;
    grandparent->parentNode = parent;

    node->leftChild = sentinel;
    node->rightChild = sentinel;

    grandparent->leftChild = sentinel;
    grandparent->rightChild = sentinel;

    root_ = parent;
}

void RedBlackTree::print()
{
    if (root_ == sentinel)
    {
        std::cout << "[]\n";
        return;
    }

    std::vector<std::string> result;
    std::queue<Node *> q;

    q.push(root_);

    while (!q.empty())
    {
        Node *node = q.front();
        q.pop();

        if (node == sentinel)
        {
            result.emplace_back("null");
        }
        else
        {
            result.emplace_back(std::to_string(node->key));

            // Push children even if sentinel so structure is preserved
            q.push(node->leftChild);
            q.push(node->rightChild);
        }
    }

    // Remove trailing nulls (same style as LeetCode)
    while (!result.empty() && result.back() == "null")
    {
        result.pop_back();
    }

    std::cout << "[";

    for (size_t i = 0; i < result.size(); i++)
    {
        std::cout << result[i];

        if (i + 1 < result.size())
        {
            std::cout << ", ";
        }
    }

    std::cout << "]\n";
}

void RedBlackTree::leftRotate(Node *x)
{
    Node *y = x->rightChild;
    if (y == sentinel)
        return;

    x->rightChild = y->leftChild;
    if (y->leftChild != sentinel)
        y->leftChild->parentNode = x;

    y->parentNode = x->parentNode;
    if (x->parentNode == sentinel)
        root_ = y;
    else if (x == x->parentNode->leftChild)
        x->parentNode->leftChild = y;
    else
        x->parentNode->rightChild = y;

    y->leftChild = x;
    x->parentNode = y;
}

void RedBlackTree::rightRotate(Node *x)
{
    Node *y = x->leftChild;
    if (y == sentinel)
        return;

    x->leftChild = y->rightChild;
    if (y->rightChild != sentinel)
        y->rightChild->parentNode = x;

    y->parentNode = x->parentNode;
    if (x->parentNode == sentinel)
        root_ = y;
    else if (x == x->parentNode->rightChild)
        x->parentNode->rightChild = y;
    else
        x->parentNode->leftChild = y;

    y->rightChild = x;
    x->parentNode = y;
}

void RedBlackTree::deleteSubtree(Node *node)
{
    if (!node || node == sentinel)
        return;

    deleteSubtree(node->leftChild);
    deleteSubtree(node->rightChild);
    delete node;
}