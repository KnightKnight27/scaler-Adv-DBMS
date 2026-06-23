#include "rbt.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

RedBlackTree::RedBlackTree() {
    NIL = new RBTNode(0, COLOR_BLACK);
    NIL->left = NIL->right = NIL->parent = NIL;
    root = NIL;
}

RedBlackTree::~RedBlackTree() {
    clearTree(root);
    delete NIL;
}

void RedBlackTree::clearTree(RBTNode* node) {
    if (node != NIL && node != nullptr) {
        clearTree(node->left);
        clearTree(node->right);
        delete node;
    }
}

RBTNode* RedBlackTree::search(int key) {
    RBTNode* current = root;
    while (current != NIL && key != current->key) {
        if (key < current->key) {
            current = current->left;
        } else {
            current = current->right;
        }
    }
    return (current == NIL) ? nullptr : current;
}

void RedBlackTree::leftRotate(RBTNode* x) {
    RBTNode* y = x->right;
    x->right = y->left;
    if (y->left != NIL) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == NIL) {
        root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

void RedBlackTree::rightRotate(RBTNode* y) {
    RBTNode* x = y->left;
    y->left = x->right;
    if (x->right != NIL) {
        x->right->parent = y;
    }
    x->parent = y->parent;
    if (y->parent == NIL) {
        root = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    x->right = y;
    y->parent = x;
}

void RedBlackTree::insert(int key) {
    RBTNode* z = new RBTNode(key, COLOR_RED);
    RBTNode* parentNode = NIL;
    RBTNode* runnerNode = root;

    // Traverse the BST to find insertion parent
    while (runnerNode != NIL) {
        parentNode = runnerNode;
        if (z->key < runnerNode->key) {
            runnerNode = runnerNode->left;
        } else {
            runnerNode = runnerNode->right;
        }
    }

    z->parent = parentNode;
    if (parentNode == NIL) {
        root = z;
    } else if (z->key < parentNode->key) {
        parentNode->left = z;
    } else {
        parentNode->right = z;
    }

    z->left = NIL;
    z->right = NIL;
    z->color = COLOR_RED; // Set color red initially

    fixupInsertion(z);
}

void RedBlackTree::fixupInsertion(RBTNode* z) {
    while (z->parent->color == COLOR_RED) {
        if (z->parent == z->parent->parent->left) {
            RBTNode* uncleNode = z->parent->parent->right; // Uncle
            
            if (uncleNode->color == COLOR_RED) {
                // Case 1: Uncle is Red (Recolor only)
                z->parent->color = COLOR_BLACK;
                uncleNode->color = COLOR_BLACK;
                z->parent->parent->color = COLOR_RED;
                z = z->parent->parent;
            } else {
                // Case 2: Uncle is Black, z is a right child (Left rotation needed)
                if (z == z->parent->right) {
                    z = z->parent;
                    leftRotate(z);
                }
                // Case 3: Uncle is Black, z is a left child (Recolor and Right rotation)
                z->parent->color = COLOR_BLACK;
                z->parent->parent->color = COLOR_RED;
                rightRotate(z->parent->parent);
            }
        } else {
            RBTNode* uncleNode = z->parent->parent->left; // Uncle
            
            if (uncleNode->color == COLOR_RED) {
                // Case 1: Uncle is Red (Recolor only)
                z->parent->color = COLOR_BLACK;
                uncleNode->color = COLOR_BLACK;
                z->parent->parent->color = COLOR_RED;
                z = z->parent->parent;
            } else {
                // Case 2: Uncle is Black, z is a left child (Right rotation needed)
                if (z == z->parent->left) {
                    z = z->parent;
                    rightRotate(z);
                }
                // Case 3: Uncle is Black, z is a right child (Recolor and Left rotation)
                z->parent->color = COLOR_BLACK;
                z->parent->parent->color = COLOR_RED;
                leftRotate(z->parent->parent);
            }
        }
    }
    root->color = COLOR_BLACK; // Root must remain black
}

void RedBlackTree::printTree() {
    if (root == NIL) {
        std::cout << " (Empty Tree)\n";
    } else {
        printTreeRecursive(root, "", true);
    }
}

void RedBlackTree::printTreeRecursive(RBTNode* node, const std::string& indent, bool isLast) {
    if (node != NIL) {
        std::cout << indent;
        if (isLast) {
            std::cout << "\033[1;30m└── \033[0m";
        } else {
            std::cout << "\033[1;30m├── \033[0m";
        }
        
        std::string colCode = (node->color == COLOR_RED) ? "\033[1;31m" : "\033[1;37m";
        std::string colName = (node->color == COLOR_RED) ? "[RED]" : "[BLACK]";
        std::cout << colCode << node->key << " " << colName << "\033[0m\n";

        std::string childIndent = indent + (isLast ? "    " : "│   ");
        if (node->left != NIL || node->right != NIL) {
            printTreeRecursive(node->right, childIndent, false);
            printTreeRecursive(node->left, childIndent, true);
        }
    }
}

int RedBlackTree::getHeight() {
    return calculateHeight(root);
}

int RedBlackTree::calculateHeight(RBTNode* node) {
    if (node == NIL || node == nullptr) return 0;
    return 1 + std::max(calculateHeight(node->left), calculateHeight(node->right));
}

int RedBlackTree::getBlackHeight() {
    return calculateBlackHeight(root);
}

int RedBlackTree::calculateBlackHeight(RBTNode* node) {
    int bh = 0;
    RBTNode* curr = node;
    while (curr != NIL) {
        if (curr->color == COLOR_BLACK) {
            bh++;
        }
        curr = curr->left;
    }
    return bh;
}

bool RedBlackTree::isRBBalanced() {
    if (root == NIL) return true;
    if (root->color != COLOR_BLACK) return false;
    int expectedBlackHeight = -1;
    return checkRBPropertiesRecursive(root, 0, expectedBlackHeight);
}

bool RedBlackTree::checkRBPropertiesRecursive(RBTNode* node, int currentBlackHeight, int& targetBlackHeight) {
    if (node == NIL) {
        if (targetBlackHeight == -1) {
            targetBlackHeight = currentBlackHeight;
            return true;
        }
        return currentBlackHeight == targetBlackHeight;
    }

    if (node->color == COLOR_RED) {
        if (node->left->color == COLOR_RED || node->right->color == COLOR_RED) {
            return false; // Consecutive Red violation
        }
    }

    int nextBlackHeight = currentBlackHeight + (node->color == COLOR_BLACK ? 1 : 0);
    return checkRBPropertiesRecursive(node->left, nextBlackHeight, targetBlackHeight) &&
           checkRBPropertiesRecursive(node->right, nextBlackHeight, targetBlackHeight);
}

bool RedBlackTree::isAVLBalanced() {
    auto verifyAVL = [this](auto& self, RBTNode* node) -> bool {
        if (node == NIL) return true;
        int lh = calculateHeight(node->left);
        int rh = calculateHeight(node->right);
        if (std::abs(lh - rh) > 1) return false;
        return self(self, node->left) && self(self, node->right);
    };
    return verifyAVL(verifyAVL, root);
}

std::vector<std::pair<int, std::pair<int, int>>> RedBlackTree::getNodeHeightsAndBalances() {
    std::vector<std::pair<int, std::pair<int, int>>> list;
    traverseHeightsAndBalances(root, list);
    return list;
}

void RedBlackTree::traverseHeightsAndBalances(RBTNode* node, std::vector<std::pair<int, std::pair<int, int>>>& list) {
    if (node != NIL) {
        traverseHeightsAndBalances(node->left, list);
        
        int lh = calculateHeight(node->left);
        int rh = calculateHeight(node->right);
        int balanceFactor = lh - rh;
        int nodeHeight = 1 + std::max(lh, rh);
        list.push_back({node->key, {nodeHeight, balanceFactor}});
        
        traverseHeightsAndBalances(node->right, list);
    }
}
