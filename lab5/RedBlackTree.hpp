#ifndef RED_BLACK_TREE_HPP
#define RED_BLACK_TREE_HPP

#include <iostream>
#include <string>
#include <stdexcept>
#include <queue>
#include <algorithm>

enum Color { RED, BLACK };

template <typename K, typename V>
class RedBlackTree {
public:
    struct Node {
        K key;
        V value;
        Color color;
        Node* left;
        Node* right;
        Node* parent;

        Node(const K& k, const V& v, Color col, Node* l = nullptr, Node* r = nullptr, Node* p = nullptr)
            : key(k), value(v), color(col), left(l), right(r), parent(p) {}
    };

    RedBlackTree() {
        NIL = new Node(K{}, V{}, BLACK);
        NIL->left = NIL;
        NIL->right = NIL;
        NIL->parent = nullptr;
        root = NIL;
    }

    ~RedBlackTree() {
        clear(root);
        delete NIL;
    }

    // Search for a key in the tree
    V* search(const K& key) {
        Node* result = searchHelper(root, key);
        if (result == NIL) {
            return nullptr;
        }
        return &(result->value);
    }

    const V* search(const K& key) const {
        Node* result = searchHelper(root, key);
        if (result == NIL) {
            return nullptr;
        }
        return &(result->value);
    }

    // Insert a key-value pair
    void insert(const K& key, const V& val) {
        Node* z = new Node(key, val, RED, NIL, NIL, nullptr);
        Node* y = nullptr;
        Node* x = root;

        while (x != NIL) {
            y = x;
            if (z->key < x->key) {
                x = x->left;
            } else if (z->key > x->key) {
                x = x->right;
            } else {
                // Key already exists, update value and clean up allocated node
                x->value = val;
                delete z;
                return;
            }
        }

        z->parent = y;
        if (y == nullptr) {
            root = z;
        } else if (z->key < y->key) {
            y->left = z;
        } else {
            y->right = z;
        }

        if (z->parent == nullptr) {
            z->color = BLACK;
            return;
        }

        if (z->parent->parent == nullptr) {
            return;
        }

        insertFixup(z);
    }

    // Remove a key from the tree
    bool remove(const K& key) {
        Node* z = searchHelper(root, key);
        if (z == NIL) {
            return false; // Key not found
        }

        Node* y = z;
        Node* x = nullptr;
        Color y_original_color = y->color;

        if (z->left == NIL) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == NIL) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);
            y_original_color = y->color;
            x = y->right;
            if (y->parent == z) {
                x->parent = y;
            } else {
                transplant(y, y->right);
                y->right = z->right;
                y->right->parent = y;
            }
            transplant(z, y);
            y->left = z->left;
            y->left->parent = y;
            y->color = z->color;
        }

        delete z;

        if (y_original_color == BLACK) {
            deleteFixup(x);
        }

        return true;
    }

    // Programmatically verify all 5 fundamental properties of a Red-Black Tree
    bool verifyRBTProperties(std::string& errorMsg) const {
        // Property 1: Nodes are either RED or BLACK (guaranteed by enum type)

        // Property 2: Root is BLACK
        if (root != NIL && root->color != BLACK) {
            errorMsg = "Root is not BLACK!";
            return false;
        }

        // Property 3: Every leaf (NIL) is BLACK (guaranteed by NIL initialization)

        // Property 4: If a node is RED, both its children are BLACK (no double REDs)
        if (!verifyNoDoubleRed(root, errorMsg)) {
            return false;
        }

        // Property 5: For each node, all simple paths to descendant leaves contain the same number of black nodes
        int expectedBlackHeight = -1;
        if (!verifyBlackHeight(root, 0, expectedBlackHeight, errorMsg)) {
            return false;
        }

        errorMsg = "All Red-Black Tree properties successfully verified.";
        return true;
    }

    // Visual tree printer
    void printTree() const {
        if (root == NIL) {
            std::cout << "[Empty Tree]" << std::endl;
            return;
        }
        printHelper(root, "", true);
    }

    // In-order traversal to print keys
    void printInOrder() const {
        inOrderHelper(root);
        std::cout << std::endl;
    }

    // Helper for debugging/testing node colors
    Color getNodeColor(const K& key) const {
        Node* node = searchHelper(root, key);
        if (node == NIL) {
            throw std::runtime_error("Key not found");
        }
        return node->color;
    }

private:
    Node* root;
    Node* NIL;

    void clear(Node* node) {
        if (node != NIL && node != nullptr) {
            clear(node->left);
            clear(node->right);
            delete node;
        }
    }

    Node* searchHelper(Node* node, const K& key) const {
        if (node == NIL || key == node->key) {
            return node;
        }
        if (key < node->key) {
            return searchHelper(node->left, key);
        }
        return searchHelper(node->right, key);
    }

    Node* minimum(Node* node) const {
        while (node->left != NIL) {
            node = node->left;
        }
        return node;
    }

    void transplant(Node* u, Node* v) {
        if (u->parent == nullptr) {
            root = v;
        } else if (u == u->parent->left) {
            u->parent->left = v;
        } else {
            u->parent->right = v;
        }
        v->parent = u->parent;
    }

    void leftRotate(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != NIL) {
            y->left->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nullptr) {
            root = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node* y) {
        Node* x = y->left;
        y->left = x->right;
        if (x->right != NIL) {
            x->right->parent = y;
        }
        x->parent = y->parent;
        if (y->parent == nullptr) {
            root = x;
        } else if (y == y->parent->left) {
            y->parent->left = x;
        } else {
            y->parent->right = x;
        }
        x->right = y;
        y->parent = x;
    }

    void insertFixup(Node* k) {
        Node* u;
        while (k->parent != nullptr && k->parent->color == RED) {
            if (k->parent == k->parent->parent->right) {
                u = k->parent->parent->left; // Uncle
                if (u->color == RED) {
                    // Case 1: Uncle is RED
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                } else {
                    if (k == k->parent->left) {
                        // Case 2: Uncle is BLACK, k is left child
                        k = k->parent;
                        rightRotate(k);
                    }
                    // Case 3: Uncle is BLACK, k is right child
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    leftRotate(k->parent->parent);
                }
            } else {
                u = k->parent->parent->right; // Uncle
                if (u->color == RED) {
                    // Case 1: Uncle is RED (mirror)
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                } else {
                    if (k == k->parent->right) {
                        // Case 2: Uncle is BLACK, k is right child (mirror)
                        k = k->parent;
                        leftRotate(k);
                    }
                    // Case 3: Uncle is BLACK, k is left child (mirror)
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    rightRotate(k->parent->parent);
                }
            }
            if (k == root) {
                break;
            }
        }
        root->color = BLACK;
    }

    void deleteFixup(Node* x) {
        Node* s;
        while (x != root && x->color == BLACK) {
            if (x == x->parent->left) {
                s = x->parent->right; // Sibling
                if (s->color == RED) {
                    // Case 1: Sibling is RED
                    s->color = BLACK;
                    x->parent->color = RED;
                    leftRotate(x->parent);
                    s = x->parent->right;
                }

                if (s->left->color == BLACK && s->right->color == BLACK) {
                    // Case 2: Sibling and its children are BLACK
                    s->color = RED;
                    x = x->parent;
                } else {
                    if (s->right->color == BLACK) {
                        // Case 3: Sibling is BLACK, right child is BLACK, left child is RED
                        s->left->color = BLACK;
                        s->color = RED;
                        rightRotate(s);
                        s = x->parent->right;
                    }
                    // Case 4: Sibling is BLACK, right child is RED
                    s->color = x->parent->color;
                    x->parent->color = BLACK;
                    s->right->color = BLACK;
                    leftRotate(x->parent);
                    x = root;
                }
            } else {
                s = x->parent->left; // Sibling (mirror)
                if (s->color == RED) {
                    // Case 1: Sibling is RED
                    s->color = BLACK;
                    x->parent->color = RED;
                    rightRotate(x->parent);
                    s = x->parent->left;
                }

                if (s->right->color == BLACK && s->left->color == BLACK) {
                    // Case 2: Sibling and its children are BLACK
                    s->color = RED;
                    x = x->parent;
                } else {
                    if (s->left->color == BLACK) {
                        // Case 3: Sibling is BLACK, left child is BLACK, right child is RED
                        s->right->color = BLACK;
                        s->color = RED;
                        leftRotate(s);
                        s = x->parent->left;
                    }
                    // Case 4: Sibling is BLACK, left child is RED
                    s->color = x->parent->color;
                    x->parent->color = BLACK;
                    s->left->color = BLACK;
                    rightRotate(x->parent);
                    x = root;
                }
            }
        }
        x->color = BLACK;
    }

    bool verifyNoDoubleRed(Node* node, std::string& errorMsg) const {
        if (node == NIL) {
            return true;
        }

        if (node->color == RED) {
            if (node->left->color == RED) {
                errorMsg = "Parent " + std::to_string(node->key) + " (RED) has left child " +
                           std::to_string(node->left->key) + " (RED). Property 4 violated!";
                return false;
            }
            if (node->right->color == RED) {
                errorMsg = "Parent " + std::to_string(node->key) + " (RED) has right child " +
                           std::to_string(node->right->key) + " (RED). Property 4 violated!";
                return false;
            }
        }

        return verifyNoDoubleRed(node->left, errorMsg) && verifyNoDoubleRed(node->right, errorMsg);
    }

    bool verifyBlackHeight(Node* node, int currentBlackHeight, int& expectedBlackHeight, std::string& errorMsg) const {
        if (node->color == BLACK) {
            currentBlackHeight++;
        }

        if (node == NIL) {
            if (expectedBlackHeight == -1) {
                expectedBlackHeight = currentBlackHeight;
            } else if (currentBlackHeight != expectedBlackHeight) {
                errorMsg = "Black height mismatch! Path to NIL leaf has black height " +
                           std::to_string(currentBlackHeight) + " but expected " +
                           std::to_string(expectedBlackHeight) + ". Property 5 violated!";
                return false;
            }
            return true;
        }

        return verifyBlackHeight(node->left, currentBlackHeight, expectedBlackHeight, errorMsg) &&
               verifyBlackHeight(node->right, currentBlackHeight, expectedBlackHeight, errorMsg);
    }

    void printHelper(Node* n, std::string indent, bool last) const {
        if (n != NIL) {
            std::cout << indent;
            if (last) {
                std::cout << "└── ";
                indent += "    ";
            } else {
                std::cout << "├── ";
                indent += "│   ";
            }
            std::string colorName = (n->color == RED) ? "R" : "B";
            std::string colorCode = (n->color == RED) ? "\033[1;31m" : "\033[1;37m"; // Bold Red / Bold White (for Black)
            std::string resetCode = "\033[0m";

            std::cout << n->key << " (" << colorCode << colorName << resetCode << "): " << n->value << std::endl;
            printHelper(n->left, indent, false);
            printHelper(n->right, indent, true);
        }
    }

    void inOrderHelper(Node* node) const {
        if (node != NIL) {
            inOrderHelper(node->left);
            std::cout << node->key << ":" << node->value << " ";
            inOrderHelper(node->right);
        }
    }
};

#endif // RED_BLACK_TREE_HPP
