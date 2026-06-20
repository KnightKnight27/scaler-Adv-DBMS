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

/*
 * Red-Black Tree Insertion Rules
 *
 * "current" refers to the newly inserted node
 *
 * Case 0 - The parent of current is black
 * Case 1 - The parent of current is red, and the uncle (parent's sibling) is also red
 * Case 2 - The parent of current is red, current is the right child, and the uncle is black
 * Case 3 - The parent of current is red, and current is the left child
 *
 * Resolution Strategy:
 *
 * Case 0 - No fix needed; insertion does not break any rules
 * Case 1 - Recolor the grandparent, parent, and uncle; then propagate upward from the grandparent
 * Case 2 - Apply a left rotation to convert this into Case 3
 * Case 3 - Apply a right rotation to restore balance
 */

RedBlackTree::RedBlackTree()
    : SENTINEL(new Node(0))
{
    root = SENTINEL;
}

RedBlackTree::~RedBlackTree()
{
    // TODO: Traverse and free all allocated nodes
}

bool RedBlackTree::search(int value)
{
    Node *current = root;

    while (current != SENTINEL) {
        if (value == current->data) {
            return true;
        } else if (value < current->data) {
            current = current->leftChild;
        } else {
            current = current->rightChild;
        }
    }

    return false;
}

void RedBlackTree::insert(int value)
{
    Node *cursor = root;
    Node *parentNode = nullptr;
    bool attachedLeft = false;

    while (cursor != SENTINEL) {
        parentNode = cursor;
        if (value <= cursor->data) {
            cursor = cursor->leftChild;
            attachedLeft = true;
        } else {
            cursor = cursor->rightChild;
            attachedLeft = false;
        }
    }

    if (cursor == root) {
        root = new Node(value);
        root->nodeColor = Color::BLACK;
        root->leftChild = SENTINEL;
        root->rightChild = SENTINEL;
    } else {
        Node *newNode = new Node(value);
        newNode->leftChild = SENTINEL;
        newNode->rightChild = SENTINEL;
        newNode->parentNode = parentNode;

        if (attachedLeft) {
            parentNode->leftChild = newNode;
        } else {
            parentNode->rightChild = newNode;
        }

        rebalance(newNode);
    }
}

void RedBlackTree::deleteNode(int value)
{
    // TO BE IMPLEMENTED
}

void RedBlackTree::rebalance(Node *current)
{
    if (checkCase0(current)) {
        resolveCase0(current);
    } else if (checkCase3(current)) {
        resolveCase3(current);
    } else if (checkCase1(current)) {
        resolveCase1(current);
    } else if (checkCase2(current)) {
        resolveCase2(current);
    } else {
        LOG("Unrecognized case encountered");
        LOG("Node: " << current->data
            << " L " << (current->leftChild ? current->leftChild->data : -1)
            << " R " << (current->rightChild ? current->rightChild->data : -1) << '\n');
    }
}

bool RedBlackTree::checkCase0(Node *current)
{
    return (current->parentNode && current->parentNode->nodeColor == Color::BLACK);
}

bool RedBlackTree::checkCase1(Node *current)
{
    Node *uncle = fetchUncle(current);
    return (current->parentNode && current->parentNode->nodeColor == Color::RED
            && uncle && uncle->nodeColor == Color::RED);
}

bool RedBlackTree::checkCase2(Node *current)
{
    Node *uncle = fetchUncle(current);
    return (current->parentNode && current->parentNode->nodeColor == Color::RED
            && uncle && uncle->nodeColor == Color::BLACK);
}

bool RedBlackTree::checkCase3(Node *current)
{
    Node *parentNode = current->parentNode;
    Node *grandparent = fetchGrandParent(current);

    bool leftAligned = (parentNode && parentNode->leftChild == current
                        && grandparent && grandparent->leftChild == parentNode
                        && parentNode->nodeColor == Color::RED);

    bool rightAligned = (parentNode && parentNode->rightChild == current
                         && grandparent && grandparent->rightChild == parentNode
                         && parentNode->nodeColor == Color::RED);

    return (leftAligned || rightAligned);
}

RedBlackTree::Node* RedBlackTree::fetchGrandParent(Node *current)
{
    if (current->parentNode && current->parentNode->parentNode) {
        return current->parentNode->parentNode;
    }
    return SENTINEL;
}

RedBlackTree::Node* RedBlackTree::fetchUncle(Node *current)
{
    Node *grandparent = fetchGrandParent(current);

    if (!current->parentNode || !grandparent) {
        return SENTINEL;
    }

    bool parentIsLeftChild = (grandparent->leftChild == current->parentNode);

    if (parentIsLeftChild) {
        return grandparent->rightChild;
    } else {
        return grandparent->leftChild;
    }
}

void RedBlackTree::resolveCase0(Node *current)
{
    LOG("case0");
    if (current->leftChild == nullptr) {
        current->leftChild = SENTINEL;
    }
    if (current->rightChild == nullptr) {
        current->rightChild = SENTINEL;
    }
}

void RedBlackTree::resolveCase1(Node *current)
{
    LOG("case1");
    Node *grandparent = fetchGrandParent(current);
    Node *uncle = fetchUncle(current);
    Node *parentNode = current->parentNode;

    parentNode->nodeColor = Color::BLACK;
    uncle->nodeColor = Color::BLACK;

    if (grandparent == root) {
        grandparent->nodeColor = Color::BLACK;
    } else {
        grandparent->nodeColor = Color::RED;
        rebalance(grandparent);
    }
}

void RedBlackTree::resolveCase2(Node *current)
{
    LOG("case2");
    Node *parentNode = current->parentNode;
    Node *grandparent = fetchGrandParent(current);

    grandparent->leftChild = current;
    current->parentNode = grandparent;
    current->leftChild = parentNode;
    parentNode->parentNode = current;

    rebalance(parentNode);
}

void RedBlackTree::resolveCase3(Node *current)
{
    LOG("case3");
    Node *parentNode = current->parentNode;
    Node *grandparent = fetchGrandParent(current);

    parentNode->leftChild = current;
    parentNode->rightChild = grandparent;
    parentNode->parentNode = grandparent->parentNode;
    grandparent->parentNode = parentNode;

    current->leftChild = SENTINEL;
    current->rightChild = SENTINEL;

    grandparent->leftChild = SENTINEL;
    grandparent->rightChild = SENTINEL;

    root = parentNode;
}

void RedBlackTree::display()
{
    if (root == SENTINEL) {
        std::cout << "[]\n";
        return;
    }

    std::vector<std::string> output;
    std::queue<Node*> nodeQueue;

    nodeQueue.push(root);

    while (!nodeQueue.empty()) {
        Node *current = nodeQueue.front();
        nodeQueue.pop();

        if (current == SENTINEL) {
            output.emplace_back("null");
        } else {
            output.emplace_back(std::to_string(current->data));
            nodeQueue.push(current->leftChild);
            nodeQueue.push(current->rightChild);
        }
    }

    while (!output.empty() && output.back() == "null") {
        output.pop_back();
    }

    std::cout << "[";
    for (size_t i = 0; i < output.size(); i++) {
        std::cout << output[i];
        if (i + 1 < output.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "]\n";
}