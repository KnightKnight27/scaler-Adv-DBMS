#ifndef RBT_HPP
#define RBT_HPP

#include <iostream>
#include <string>
#include <limits>
#include <algorithm>

enum Color { RED, BLACK };

struct Node {
    int key;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    Node(int k = 0, Color c = BLACK) 
        : key(k), color(c), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
public:
    RedBlackTree() {
        NIL = new Node(0, BLACK);
        NIL->left = NIL;
        NIL->right = NIL;
        NIL->parent = NIL;
        root = NIL;
    }

    ~RedBlackTree() {
        destroyHelper(root);
        delete NIL;
    }

    void insert(int key) {
        // Prevent inserting duplicate keys for standard BST behavior
        if (search(key) != NIL) {
            std::cout << "Key " << key << " already exists in the tree. No duplicates allowed.\n";
            return;
        }

        Node* z = new Node(key, RED);
        z->left = NIL;
        z->right = NIL;
        z->parent = NIL;

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

        if (z->parent == NIL) {
            z->color = BLACK;
            return;
        }

        if (z->parent->parent == NIL) {
            return;
        }

        insertFixup(z);
    }

    void remove(int key) {
        Node* z = search(key);
        if (z == NIL) {
            std::cout << "Key " << key << " not found in the tree.\n";
            return;
        }

        Node* y = z;
        Color y_original_color = y->color;
        Node* x;

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
            removeFixup(x);
        }
    }

    Node* search(int key) const {
        Node* current = root;
        while (current != NIL && key != current->key) {
            if (key < current->key) {
                current = current->left;
            } else {
                current = current->right;
            }
        }
        return current;
    }

    void inorder() const {
        inorderHelper(root);
        std::cout << "\n";
    }

    void preorder() const {
        preorderHelper(root);
        std::cout << "\n";
    }

    void postorder() const {
        postorderHelper(root);
        std::cout << "\n";
    }

    void printTree() const {
        if (root == NIL) {
            std::cout << "[Tree is empty]\n";
        } else {
            printTreeHelper(root, "", true);
        }
    }

    // Programmatic verification of the 5 RBT properties
    bool validateProperties() const {
        if (root == NIL) {
            return true; // Empty tree is a valid RBT
        }

        // Rule 2: Root must be BLACK
        if (root->color != BLACK) {
            std::cerr << "Violation: Root node " << root->key << " is not BLACK!\n";
            return false;
        }

        // Rule 3: NIL leaf node is BLACK. Our constructor ensures NIL is BLACK.

        // Check Rule 4 (no two consecutive REDs) and Rule 5 (equal black heights)
        bool properties_ok = true;
        validateBlackHeight(root, properties_ok);
        if (!properties_ok) {
            return false;
        }

        // Validate Binary Search Tree (BST) property
        if (!validateBST(root, std::numeric_limits<int>::min(), std::numeric_limits<int>::max())) {
            return false;
        }

        return true;
    }

    Node* getRoot() const {
        return root;
    }

    Node* getNil() const {
        return NIL;
    }

private:
    Node* root;
    Node* NIL;

    void leftRotate(Node* x) {
        std::cout << "  LEFT ROTATE at node " << x->key << "\n";
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

    void rightRotate(Node* y) {
        std::cout << "  RIGHT ROTATE at node " << y->key << "\n";
        Node* x = y->left;
        y->left = x->right;
        if (x->right != NIL) {
            x->right->parent = y;
        }
        x->parent = y->parent;
        if (y->parent == NIL) {
            root = x;
        } else if (y == y->parent->left) {
            y->parent->left = x;
        } else {
            y->parent->right = x;
        }
        x->right = y;
        y->parent = x;
    }

    void insertFixup(Node* z) {
        while (z->parent->color == RED) {
            if (z->parent == z->parent->parent->left) {
                Node* y = z->parent->parent->right; // Uncle
                if (y->color == RED) {
                    // Case 1: Uncle is RED
                    z->parent->color = BLACK;
                    y->color = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        // Case 2: Uncle is BLACK, z is right child
                        z = z->parent;
                        leftRotate(z);
                    }
                    // Case 3: Uncle is BLACK, z is left child
                    z->parent->color = BLACK;
                    z->parent->parent->color = RED;
                    rightRotate(z->parent->parent);
                }
            } else {
                Node* y = z->parent->parent->left; // Uncle
                if (y->color == RED) {
                    // Case 1: Uncle is RED
                    z->parent->color = BLACK;
                    y->color = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        // Case 2: Uncle is BLACK, z is left child
                        z = z->parent;
                        rightRotate(z);
                    }
                    // Case 3: Uncle is BLACK, z is right child
                    z->parent->color = BLACK;
                    z->parent->parent->color = RED;
                    leftRotate(z->parent->parent);
                }
            }
        }
        root->color = BLACK;
    }

    void transplant(Node* u, Node* v) {
        if (u->parent == NIL) {
            root = v;
        } else if (u == u->parent->left) {
            u->parent->left = v;
        } else {
            u->parent->right = v;
        }
        v->parent = u->parent;
    }

    Node* minimum(Node* node) {
        while (node->left != NIL) {
            node = node->left;
        }
        return node;
    }

    void removeFixup(Node* x) {
        while (x != root && x->color == BLACK) {
            if (x == x->parent->left) {
                Node* w = x->parent->right; // Sibling
                if (w->color == RED) {
                    // Case 1: Sibling is RED
                    w->color = BLACK;
                    x->parent->color = RED;
                    leftRotate(x->parent);
                    w = x->parent->right;
                }
                if (w->left->color == BLACK && w->right->color == BLACK) {
                    // Case 2: Sibling's children are both BLACK
                    w->color = RED;
                    x = x->parent;
                } else {
                    if (w->right->color == BLACK) {
                        // Case 3: Sibling's right child is BLACK, left is RED
                        w->left->color = BLACK;
                        w->color = RED;
                        rightRotate(w);
                        w = x->parent->right;
                    }
                    // Case 4: Sibling's right child is RED
                    w->color = x->parent->color;
                    x->parent->color = BLACK;
                    w->right->color = BLACK;
                    leftRotate(x->parent);
                    x = root;
                }
            } else {
                Node* w = x->parent->left; // Sibling
                if (w->color == RED) {
                    // Case 1: Sibling is RED
                    w->color = BLACK;
                    x->parent->color = RED;
                    rightRotate(x->parent);
                    w = x->parent->left;
                }
                if (w->right->color == BLACK && w->left->color == BLACK) {
                    // Case 2: Sibling's children are both BLACK
                    w->color = RED;
                    x = x->parent;
                } else {
                    if (w->left->color == BLACK) {
                        // Case 3: Sibling's left child is BLACK, right is RED
                        w->right->color = BLACK;
                        w->color = RED;
                        leftRotate(w);
                        w = x->parent->left;
                    }
                    // Case 4: Sibling's left child is RED
                    w->color = x->parent->color;
                    x->parent->color = BLACK;
                    w->left->color = BLACK;
                    rightRotate(x->parent);
                    x = root;
                }
            }
        }
        x->color = BLACK;
    }

    void destroyHelper(Node* node) {
        if (node != NIL && node != nullptr) {
            destroyHelper(node->left);
            destroyHelper(node->right);
            delete node;
        }
    }

    void inorderHelper(Node* node) const {
        if (node != NIL) {
            inorderHelper(node->left);
            std::cout << node->key << " ";
            inorderHelper(node->right);
        }
    }

    void preorderHelper(Node* node) const {
        if (node != NIL) {
            std::cout << node->key << " ";
            preorderHelper(node->left);
            preorderHelper(node->right);
        }
    }

    void postorderHelper(Node* node) const {
        if (node != NIL) {
            postorderHelper(node->left);
            postorderHelper(node->right);
            std::cout << node->key << " ";
        }
    }

    void printTreeHelper(Node* node, std::string indent, bool last) const {
        if (node != NIL) {
            std::cout << indent;
            if (last) {
                std::cout << "R----";
                indent += "   ";
            } else {
                std::cout << "L----";
                indent += "|  ";
            }
            
            // Output node key and color
            if (node->color == RED) {
                // RED console color: bold red (\033[1;31m) and reset (\033[0m)
                std::cout << "\033[1;31m" << node->key << " [R]\033[0m\n";
            } else {
                std::cout << node->key << " [B]\n";
            }
            printTreeHelper(node->left, indent, false);
            printTreeHelper(node->right, indent, true);
        }
    }

    int validateBlackHeight(Node* node, bool& ok) const {
        if (!ok) return 0;
        if (node == NIL) return 1;

        // Rule 4: If a node is RED, both its children must be BLACK
        if (node->color == RED) {
            if (node->left->color == RED || node->right->color == RED) {
                std::cerr << "Violation: RED node " << node->key << " has a RED child!\n";
                ok = false;
                return 0;
            }
        }

        // Rule 5: For each node, all simple paths to descendant leaves contain the same number of black nodes
        int leftHeight = validateBlackHeight(node->left, ok);
        int rightHeight = validateBlackHeight(node->right, ok);

        if (leftHeight != rightHeight) {
            std::cerr << "Violation: Node " << node->key << " has left black height " 
                      << leftHeight << " but right black height " << rightHeight << "!\n";
            ok = false;
            return 0;
        }

        return leftHeight + (node->color == BLACK ? 1 : 0);
    }

    bool validateBST(Node* node, int min_val, int max_val) const {
        if (node == NIL) return true;
        if (node->key < min_val || node->key > max_val) {
            std::cerr << "Violation: BST property violated at node " << node->key << " (value out of range [" << min_val << ", " << max_val << "])\n";
            return false;
        }
        
        int next_min = (node->key == std::numeric_limits<int>::min()) ? node->key : node->key + 1;
        int next_max = (node->key == std::numeric_limits<int>::max()) ? node->key : node->key - 1;

        return validateBST(node->left, min_val, next_max) && validateBST(node->right, next_min, max_val);
    }
};

#endif // RBT_HPP
