#include "RedBlackTree.h"
#include <iostream>
#include <queue>

Node::Node(int val)
    : data(val), color(RED),
      left(nullptr), right(nullptr), parent(nullptr) {}

RedBlackTree::RedBlackTree() : root(nullptr) {}

RedBlackTree::~RedBlackTree() {
    destroyTree(root);
}

void RedBlackTree::destroyTree(Node* node) {
    if (node == nullptr) return;
    destroyTree(node->left);
    destroyTree(node->right);
    delete node;
}

void RedBlackTree::rotateLeft(Node* x) {
    Node* y  = x->right;
    x->right = y->left;

    if (y->left != nullptr)
        y->left->parent = x;

    y->parent = x->parent;

    if (x->parent == nullptr)
        root = y;
    else if (x == x->parent->left)
        x->parent->left  = y;
    else
        x->parent->right = y;

    y->left   = x;
    x->parent = y;
}

void RedBlackTree::rotateRight(Node* y) {
    Node* x  = y->left;
    y->left  = x->right;

    if (x->right != nullptr)
        x->right->parent = y;

    x->parent = y->parent;

    if (y->parent == nullptr)
        root = x;
    else if (y == y->parent->left)
        y->parent->left  = x;
    else
        y->parent->right = x;

    x->right  = y;
    y->parent = x;
}

void RedBlackTree::insert(int val) {
    Node* z = new Node(val);

    Node* parent  = nullptr;
    Node* current = root;

    while (current != nullptr) {
        parent = current;
        if (z->data < current->data)
            current = current->left;
        else if (z->data > current->data)
            current = current->right;
        else {
            delete z;   
            return;
        }
    }

    z->parent = parent;

    if (parent == nullptr)
        root = z;                        
    else if (z->data < parent->data)
        parent->left  = z;
    else
        parent->right = z;

    fixInsert(z);
}

void RedBlackTree::fixInsert(Node* z) {
    while (z->parent != nullptr && z->parent->color == RED) {

        Node* parent      = z->parent;
        Node* grandparent = parent->parent;
        if (grandparent == nullptr) break;

        if (parent == grandparent->left) {
            Node* uncle = grandparent->right;

            if (uncle != nullptr && uncle->color == RED) {
                parent->color      = BLACK;
                uncle->color       = BLACK;
                grandparent->color = RED;
                z = grandparent;

            } else {
                if (z == parent->right) {
                    z = parent;
                    rotateLeft(z);
                    parent      = z->parent;
                    grandparent = parent->parent;
                }
                parent->color      = BLACK;
                grandparent->color = RED;
                rotateRight(grandparent);
            }

        } else {
            Node* uncle = grandparent->left;

            if (uncle != nullptr && uncle->color == RED) {
                parent->color      = BLACK;
                uncle->color       = BLACK;
                grandparent->color = RED;
                z = grandparent;

            } else {
                if (z == parent->left) {
                    z = parent;
                    rotateRight(z);
                    parent      = z->parent;
                    grandparent = parent->parent;
                }
                parent->color      = BLACK;
                grandparent->color = RED;
                rotateLeft(grandparent);
            }
        }
    }
    root->color = BLACK;
}

void RedBlackTree::transplant(Node* u, Node* v) {
    if (u->parent == nullptr)
        root = v;
    else if (u == u->parent->left)
        u->parent->left  = v;
    else
        u->parent->right = v;

    if (v != nullptr)
        v->parent = u->parent;
}

Node* RedBlackTree::minimum(Node* node) const {
    while (node->left != nullptr)
        node = node->left;
    return node;
}

void RedBlackTree::remove(int val) {
    Node* z = root;
    while (z != nullptr) {
        if      (val < z->data) z = z->left;
        else if (val > z->data) z = z->right;
        else break;
    }
    if (z == nullptr) return; 

    Node* y        = z;           
    Node* x        = nullptr;     
    Node* xParent  = nullptr;     
    Color yOriginalColor = y->color;

    if (z->left == nullptr) {
        x       = z->right;
        xParent = z->parent;
        transplant(z, z->right);

    } else if (z->right == nullptr) {
        x       = z->left;
        xParent = z->parent;
        transplant(z, z->left);

    } else {
        y             = minimum(z->right);
        yOriginalColor = y->color;
        x              = y->right;

        if (y->parent == z) {
            xParent = y;
        } else {
            xParent = y->parent;
            transplant(y, y->right);
            y->right         = z->right;
            y->right->parent = y;
        }
        transplant(z, y);
        y->left         = z->left;
        y->left->parent = y;
        y->color        = z->color;
    }

    delete z;

    if (yOriginalColor == BLACK)
        fixDelete(x, xParent);
}

void RedBlackTree::fixDelete(Node* x, Node* xParent) {
    while (x != root && (x == nullptr || x->color == BLACK)) {

        if (x == (xParent ? xParent->left : nullptr)) {
            Node* sibling = xParent ? xParent->right : nullptr;

            if (sibling != nullptr && sibling->color == RED) {
                sibling->color  = BLACK;
                xParent->color  = RED;
                rotateLeft(xParent);
                sibling = xParent->right;
            }

            if (sibling == nullptr) {
                x = xParent;
                xParent = x->parent;
            } else if ((sibling->left  == nullptr || sibling->left->color  == BLACK) &&
                       (sibling->right == nullptr || sibling->right->color == BLACK)) {
                sibling->color = RED;
                x       = xParent;
                xParent = x->parent;

            } else {
                if (sibling->right == nullptr || sibling->right->color == BLACK) {
                    if (sibling->left != nullptr)
                        sibling->left->color = BLACK;
                    sibling->color = RED;
                    rotateRight(sibling);
                    sibling = xParent->right;
                }
                sibling->color = xParent->color;
                xParent->color = BLACK;
                if (sibling->right != nullptr)
                    sibling->right->color = BLACK;
                rotateLeft(xParent);
                x = root;
            }

        } else {
            Node* sibling = xParent ? xParent->left : nullptr;

            if (sibling != nullptr && sibling->color == RED) {
                sibling->color = BLACK;
                xParent->color = RED;
                rotateRight(xParent);
                sibling = xParent->left;
            }

            if (sibling == nullptr) {
                x = xParent;
                xParent = x->parent;
            } else if ((sibling->right == nullptr || sibling->right->color == BLACK) &&
                       (sibling->left  == nullptr || sibling->left->color  == BLACK)) {
                sibling->color = RED;
                x       = xParent;
                xParent = x->parent;

            } else {
                if (sibling->left == nullptr || sibling->left->color == BLACK) {
                    if (sibling->right != nullptr)
                        sibling->right->color = BLACK;
                    sibling->color = RED;
                    rotateLeft(sibling);
                    sibling = xParent->left;
                }
                sibling->color = xParent->color;
                xParent->color = BLACK;
                if (sibling->left != nullptr)
                    sibling->left->color = BLACK;
                rotateRight(xParent);
                x = root;
            }
        }
    }
    if (x != nullptr)
        x->color = BLACK;
}

bool RedBlackTree::search(int val) const {
    Node* current = root;
    while (current != nullptr) {
        if      (val < current->data) current = current->left;
        else if (val > current->data) current = current->right;
        else                          return true;
    }
    return false;
}

void RedBlackTree::inorder(Node* node) const {
    if (node == nullptr) return;
    inorder(node->left);
    std::cout << node->data
              << "(" << (node->color == RED ? "R" : "B") << ") ";
    inorder(node->right);
}

void RedBlackTree::printInorder() const {
    std::cout << "Inorder : ";
    inorder(root);
    std::cout << "\n";
}

void RedBlackTree::printLevels() const {
    if (root == nullptr) {
        std::cout << "(empty tree)\n";
        return;
    }
    std::queue<Node*> q;
    q.push(root);
    int level = 0;
    while (!q.empty()) {
        int sz = static_cast<int>(q.size());
        std::cout << "  Level " << level << ": ";
        for (int i = 0; i < sz; i++) {
            Node* node = q.front(); q.pop();
            std::cout << node->data
                      << "(" << (node->color == RED ? "R" : "B") << ") ";
            if (node->left)  q.push(node->left);
            if (node->right) q.push(node->right);
        }
        std::cout << "\n";
        level++;
    }
}

int RedBlackTree::blackHeight(Node* node) const {
    if (node == nullptr) return 1;
    int left  = blackHeight(node->left);
    int right = blackHeight(node->right);
    if (left == -1 || right == -1) return -1;
    if (left != right)             return -1;
    return left + (node->color == BLACK ? 1 : 0);
}

bool RedBlackTree::noConsecutiveRed(Node* node) const {
    if (node == nullptr) return true;
    if (node->color == RED) {
        if ((node->left  != nullptr && node->left->color  == RED) ||
            (node->right != nullptr && node->right->color == RED))
            return false;
    }
    return noConsecutiveRed(node->left) && noConsecutiveRed(node->right);
}

bool RedBlackTree::isValid() const {
    if (root == nullptr) return true;

    if (root->color != BLACK) {
        std::cout << "  [FAIL] Rule 2: root is not BLACK\n";
        return false;
    }
    if (!noConsecutiveRed(root)) {
        std::cout << "  [FAIL] Rule 3: consecutive RED nodes found\n";
        return false;
    }
    if (blackHeight(root) == -1) {
        std::cout << "  [FAIL] Rule 4: black-height mismatch\n";
        return false;
    }
    return true;
}
