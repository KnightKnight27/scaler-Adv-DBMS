#include "rbt.hpp"
#include <algorithm>
#include <cmath>

RedBlackTree::RedBlackTree() {
    NIL = new Node(0, BLACK);
    NIL->left = NIL->right = NIL->parent = NIL;
    root = NIL;
}

RedBlackTree::~RedBlackTree() {
    deleteTree(root);
    delete NIL;
}

void RedBlackTree::deleteTree(Node* node) {
    if (node != NIL) {
        deleteTree(node->left);
        deleteTree(node->right);
        delete node;
    }
}

Node* RedBlackTree::search(int key) {
    Node* curr = root;
    while (curr != NIL && key != curr->key) {
        if (key < curr->key) {
            curr = curr->left;
        } else {
            curr = curr->right;
        }
    }
    return (curr == NIL) ? nullptr : curr;
}

void RedBlackTree::leftRotate(Node* x) {
    Node* y = x->right;
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

void RedBlackTree::rightRotate(Node* y) {
    Node* x = y->left;
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
    Node* z = new Node(key, RED);
    Node* y = NIL;
    Node* x = root;

    while (x != NIL) {
        y = x;
        if (z->key < x->key) {
            x = x->left;
        } else {
            x = x->right;
        }
    }

    z->parent = y;
    if (y == NIL) {
        root = z;
    } else if (z->key < y->key) {
        y->left = z;
    } else {
        y->right = z;
    }

    z->left = NIL;
    z->right = NIL;
    z->color = RED;

    insertFixUp(z);
}

void RedBlackTree::insertFixUp(Node* z) {
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            Node* y = z->parent->parent->right; // Uncle
            if (y->color == RED) {
                // Case 1: Uncle is Red (Recolor only)
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                // Case 2: Uncle is Black, z is right child (Left rotation needed)
                if (z == z->parent->right) {
                    z = z->parent;
                    leftRotate(z);
                }
                // Case 3: Uncle is Black, z is left child (Recolor and Right rotation)
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rightRotate(z->parent->parent);
            }
        } else {
            Node* y = z->parent->parent->left; // Uncle
            if (y->color == RED) {
                // Case 1: Uncle is Red (Recolor only)
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                // Case 2: Uncle is Black, z is left child (Right rotation needed)
                if (z == z->parent->left) {
                    z = z->parent;
                    rightRotate(z);
                }
                // Case 3: Uncle is Black, z is right child (Recolor and Left rotation)
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                leftRotate(z->parent->parent);
            }
        }
    }
    root->color = BLACK;
}

void RedBlackTree::printTree() {
    if (root == NIL) {
        std::cout << "(Empty Tree)\n";
    } else {
        printTreeHelper(root, "", true);
    }
}

void RedBlackTree::printTreeHelper(Node* node, const std::string& indent, bool last) {
    if (node != NIL) {
        std::cout << indent;
        if (last) {
            std::cout << "\033[1;30m└── \033[0m";
        } else {
            std::cout << "\033[1;30m├── \033[0m";
        }
        
        std::string colorCode = (node->color == RED) ? "\033[1;31m" : "\033[1;37m";
        std::string colorLabel = (node->color == RED) ? "[RED]" : "[BLACK]";
        std::cout << colorCode << node->key << " " << colorLabel << "\033[0m\n";

        std::string nextIndent = indent + (last ? "    " : "│   ");
        if (node->left != NIL || node->right != NIL) {
            printTreeHelper(node->right, nextIndent, false);
            printTreeHelper(node->left, nextIndent, true);
        }
    }
}

int RedBlackTree::getHeight() {
    return calculateHeight(root);
}

int RedBlackTree::calculateHeight(Node* node) {
    if (node == NIL || node == nullptr) return 0;
    return 1 + std::max(calculateHeight(node->left), calculateHeight(node->right));
}

int RedBlackTree::getBlackHeight() {
    return calculateBlackHeight(root);
}

int RedBlackTree::calculateBlackHeight(Node* node) {
    int bh = 0;
    Node* curr = node;
    while (curr != NIL) {
        if (curr->color == BLACK) {
            bh++;
        }
        curr = curr->left;
    }
    return bh;
}

bool RedBlackTree::isRBBalanced() {
    if (root == NIL) return true;
    if (root->color != BLACK) return false;
    int expectedBlackHeight = -1;
    return checkRBProperties(root, 0, expectedBlackHeight);
}

bool RedBlackTree::checkRBProperties(Node* node, int currentBlackHeight, int& expectedBlackHeight) {
    if (node == NIL) {
        if (expectedBlackHeight == -1) {
            expectedBlackHeight = currentBlackHeight;
            return true;
        }
        return currentBlackHeight == expectedBlackHeight;
    }

    if (node->color == RED) {
        if (node->left->color == RED || node->right->color == RED) {
            return false; // Consecutive Red nodes violation
        }
    }

    int nextBlackHeight = currentBlackHeight + (node->color == BLACK ? 1 : 0);
    return checkRBProperties(node->left, nextBlackHeight, expectedBlackHeight) &&
           checkRBProperties(node->right, nextBlackHeight, expectedBlackHeight);
}

bool RedBlackTree::isAVLBalanced() {
    auto verifyAVL = [this](auto& self, Node* node) -> bool {
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
    collectHeightsAndBalances(root, list);
    return list;
}

void RedBlackTree::collectHeightsAndBalances(Node* node, std::vector<std::pair<int, std::pair<int, int>>>& list) {
    if (node != NIL) {
        collectHeightsAndBalances(node->left, list);
        
        int lh = calculateHeight(node->left);
        int rh = calculateHeight(node->right);
        int balanceFactor = lh - rh;
        int nodeHeight = 1 + std::max(lh, rh);
        list.push_back({node->key, {nodeHeight, balanceFactor}});
        
        collectHeightsAndBalances(node->right, list);
    }
}
