#include <iostream>

using namespace std;

// Using an enum for better type safety and code readability
enum Color { RED, BLACK };

struct RBTNode {
    int key;
    Color color;
    RBTNode *left, *right, *parent;

    RBTNode(int val) : key(val), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
private:
    RBTNode* root;

    // Left Rotate utility
    void leftRotate(RBTNode*& rootNode, RBTNode*& x) {
        RBTNode* y = x->right;
        x->right = y->left;

        if (y->left != nullptr) {
            y->left->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            rootNode = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }

        y->left = x;
        x->parent = y;
    }

    // Right Rotate utility
    void rightRotate(RBTNode*& rootNode, RBTNode*& x) {
        RBTNode* y = x->left;
        x->left = y->right;

        if (y->right != nullptr) {
            y->right->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            rootNode = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }

        y->right = x;
        x->parent = y;
    }

    // Balance insertion violations
    void balanceInsert(RBTNode*& rootNode, RBTNode*& z) {
        while (z->parent != nullptr && z->parent->color == RED) {
            RBTNode* parentNode = z->parent;
            RBTNode* grandparent = parentNode->parent;

            // Parent is left child of grandparent
            if (parentNode == grandparent->left) {
                RBTNode* uncle = grandparent->right;

                // Case 1: Uncle is Red -> Recoloring
                if (uncle != nullptr && uncle->color == RED) {
                    parentNode->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;
                    z = grandparent;
                } else {
                    // Case 2: z is right child -> Left Rotate first
                    if (z == parentNode->right) {
                        leftRotate(rootNode, parentNode);
                        z = parentNode;
                        parentNode = z->parent;
                    }
                    // Case 3: z is left child -> Right Rotate and Swap colors
                    rightRotate(rootNode, grandparent);
                    swap(parentNode->color, grandparent->color);
                    z = parentNode;
                }
            } 
            // Parent is right child of grandparent
            else {
                RBTNode* uncle = grandparent->left;

                // Case 1: Uncle is Red -> Recoloring
                if (uncle != nullptr && uncle->color == RED) {
                    parentNode->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;
                    z = grandparent;
                } else {
                    // Case 2: z is left child -> Right Rotate first
                    if (z == parentNode->left) {
                        rightRotate(rootNode, parentNode);
                        z = parentNode;
                        parentNode = z->parent;
                    }
                    // Case 3: z is right child -> Left Rotate and Swap colors
                    leftRotate(rootNode, grandparent);
                    swap(parentNode->color, grandparent->color);
                    z = parentNode;
                }
            }
        }
        rootNode->color = BLACK;
    }

    void inOrderHelper(RBTNode* node) const {
        if (node == nullptr) return;
        inOrderHelper(node->left);
        cout << node->key << "(" << (node->color == RED ? "R" : "B") << ") ";
        inOrderHelper(node->right);
    }

    void destroyTree(RBTNode* node) {
        if (node != nullptr) {
            destroyTree(node->left);
            destroyTree(node->right);
            delete node;
        }
    }

public:
    RedBlackTree() : root(nullptr) {}

    ~RedBlackTree() {
        destroyTree(root);
    }

    // Insert a new key
    void insert(int key) {
        RBTNode* z = new RBTNode(key);
        RBTNode* parent = nullptr;
        RBTNode* current = root;

        while (current != nullptr) {
            parent = current;
            if (z->key < current->key) {
                current = current->left;
            } else {
                current = current->right;
            }
        }

        z->parent = parent;
        if (parent == nullptr) {
            root = z;
        } else if (z->key < parent->key) {
            parent->left = z;
        } else {
            parent->right = z;
        }

        balanceInsert(root, z);
    }

    // Search for a key
    bool contains(int key) const {
        RBTNode* current = root;
        while (current != nullptr) {
            if (key == current->key) {
                return true;
            } else if (key < current->key) {
                current = current->left;
            } else {
                current = current->right;
            }
        }
        return false;
    }

    // Print the tree contents in sorted order
    void print() const {
        inOrderHelper(root);
        cout << "\n";
    }
};

int main() {
    RedBlackTree tree;

    // Inserting a custom set of test keys
    tree.insert(45);
    tree.insert(26);
    tree.insert(72);
    tree.insert(18);
    tree.insert(35);
    tree.insert(10);

    cout << "Red-Black Tree In-order (sorted): ";
    tree.print();

    // Verify search operations
    int test1 = 18;
    int test2 = 99;

    cout << "Search " << test1 << ": " << (tree.contains(test1) ? "Found" : "Not Found") << "\n";
    cout << "Search " << test2 << ": " << (tree.contains(test2) ? "Found" : "Not Found") << "\n";

    return 0;
}
