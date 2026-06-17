#include "RBTree.h"

#include <iostream>
#include <queue>
#include <vector>

RBTree::RBTree() {
    sentinel = new TreeNode(0, nullptr);

    sentinel->shade = BLACK;
    sentinel->leftChild = sentinel;
    sentinel->rightChild = sentinel;

    rootNode = sentinel;
}

RBTree::~RBTree() {
    destroy(rootNode);
    delete sentinel;
}

void RBTree::destroy(TreeNode* node) {
    if (node == sentinel)
        return;

    destroy(node->leftChild);
    destroy(node->rightChild);

    delete node;
}

bool RBTree::contains(int value) const {
    TreeNode* current = rootNode;

    while (current != sentinel) {

        if (current->key == value)
            return true;

        if (value < current->key)
            current = current->leftChild;
        else
            current = current->rightChild;
    }

    return false;
}

void RBTree::leftShift(TreeNode* node) {

    TreeNode* child = node->rightChild;

    node->rightChild = child->leftChild;

    if (child->leftChild != sentinel)
        child->leftChild->parentNode = node;

    child->parentNode = node->parentNode;

    if (node->parentNode == nullptr)
        rootNode = child;

    else if (node == node->parentNode->leftChild)
        node->parentNode->leftChild = child;

    else
        node->parentNode->rightChild = child;

    child->leftChild = node;
    node->parentNode = child;
}

void RBTree::rightShift(TreeNode* node) {

    TreeNode* child = node->leftChild;

    node->leftChild = child->rightChild;

    if (child->rightChild != sentinel)
        child->rightChild->parentNode = node;

    child->parentNode = node->parentNode;

    if (node->parentNode == nullptr)
        rootNode = child;

    else if (node == node->parentNode->rightChild)
        node->parentNode->rightChild = child;

    else
        node->parentNode->leftChild = child;

    child->rightChild = node;
    node->parentNode = child;
}

void RBTree::insertValue(int value) {

    TreeNode* newNode = new TreeNode(value, sentinel);

    TreeNode* previous = nullptr;
    TreeNode* current = rootNode;

    while (current != sentinel) {

        previous = current;

        if (value < current->key)
            current = current->leftChild;
        else
            current = current->rightChild;
    }

    newNode->parentNode = previous;

    if (previous == nullptr)
        rootNode = newNode;

    else if (value < previous->key)
        previous->leftChild = newNode;

    else
        previous->rightChild = newNode;

    if (newNode->parentNode == nullptr) {
        newNode->shade = BLACK;
        return;
    }

    if (newNode->parentNode->parentNode == nullptr)
        return;

    balanceInsertion(newNode);
}

void RBTree::balanceInsertion(TreeNode* node) {

    while (node->parentNode &&
           nodeIsRed(node->parentNode)) {

        TreeNode* parent = node->parentNode;
        TreeNode* grand = parent->parentNode;

        if (parent == grand->leftChild) {

            TreeNode* uncle = grand->rightChild;

            if (nodeIsRed(uncle)) {

                parent->shade = BLACK;
                uncle->shade = BLACK;
                grand->shade = RED;

                node = grand;
            }
            else {

                if (node == parent->rightChild) {
                    node = parent;
                    leftShift(node);

                    parent = node->parentNode;
                    grand = parent->parentNode;
                }

                parent->shade = BLACK;
                grand->shade = RED;

                rightShift(grand);
            }
        }
        else {

            TreeNode* uncle = grand->leftChild;

            if (nodeIsRed(uncle)) {

                parent->shade = BLACK;
                uncle->shade = BLACK;
                grand->shade = RED;

                node = grand;
            }
            else {

                if (node == parent->leftChild) {

                    node = parent;
                    rightShift(node);

                    parent = node->parentNode;
                    grand = parent->parentNode;
                }

                parent->shade = BLACK;
                grand->shade = RED;

                leftShift(grand);
            }
        }
    }

    rootNode->shade = BLACK;
}

void RBTree::display() const {

    if (rootNode == sentinel) {
        std::cout << "Tree Empty\n";
        return;
    }

    std::queue<TreeNode*> q;
    q.push(rootNode);

    std::vector<std::string> result;

    while (!q.empty()) {

        TreeNode* current = q.front();
        q.pop();

        if (current == sentinel) {

            result.push_back("N");
            continue;
        }

        std::string nodeText =
            std::to_string(current->key);

        nodeText +=
            (current->shade == RED)
            ? "[R]"
            : "[B]";

        result.push_back(nodeText);

        q.push(current->leftChild);
        q.push(current->rightChild);
    }

    while (!result.empty() &&
           result.back() == "N") {
        result.pop_back();
    }

    std::cout << "{ ";

    for (size_t i = 0; i < result.size(); ++i) {

        std::cout << result[i];

        if (i + 1 < result.size())
            std::cout << " | ";
    }

    std::cout << " }\n";
}