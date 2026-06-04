#include <iostream>
#include <string>
#include <queue>
#include <iomanip>

enum Color { RED, BLACK };

struct Node {
    int data;
    Color color;
    Node *left, *right, *parent;

    explicit Node(int val) 
        : data(val), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
private:
    Node *root;
    Node *TNULL; // Sentinel node representing leaf nodes (always black)

    void leftRotate(Node *x) {
        std::cout << "[ROTATION] Left rotating about Node (" << x->data << ")\n";
        Node *y = x->right;
        x->right = y->left;
        if (y->left != TNULL) {
            y->left->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nullptr) {
            this->root = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node *y) {
        std::cout << "[ROTATION] Right rotating about Node (" << y->data << ")\n";
        Node *x = y->left;
        y->left = x->right;
        if (x->right != TNULL) {
            x->right->parent = y;
        }
        x->parent = y->parent;
        if (y->parent == nullptr) {
            this->root = x;
        } else if (y == y->parent->right) {
            y->parent->right = x;
        } else {
            y->parent->left = x;
        }
        x->right = y;
        y->parent = x;
    }

    void fixInsert(Node *k) {
        Node *u;
        while (k->parent != nullptr && k->parent->color == RED) {
            if (k->parent == k->parent->parent->right) {
                u = k->parent->parent->left; // uncle
                if (u->color == RED) {
                    // Case 1: Uncle is RED -> Recolor parent, uncle, and grandparent
                    std::cout << "[BALANCING] Case 1 (Uncle RED): Recoloring Node (" << k->parent->data 
                              << ") and Node (" << u->data << ") to BLACK, and Grandparent (" 
                              << k->parent->parent->data << ") to RED\n";
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                } else {
                    // Case 2 & 3: Uncle is BLACK
                    if (k == k->parent->left) {
                        // Case 2: Triangle structure -> Right rotate parent first
                        k = k->parent;
                        std::cout << "[BALANCING] Case 2 (Triangle, Uncle BLACK): Inner child. Left-Right adjustment.\n";
                        rightRotate(k);
                    }
                    // Case 3: Line structure -> Recoloring and Left rotate grandparent
                    std::cout << "[BALANCING] Case 3 (Line, Uncle BLACK): Outer child. Recoloring Parent (" 
                              << k->parent->data << ") to BLACK, Grandparent (" 
                              << k->parent->parent->data << ") to RED.\n";
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    leftRotate(k->parent->parent);
                }
            } else {
                u = k->parent->parent->right; // uncle

                if (u->color == RED) {
                    // Case 1: Uncle is RED
                    std::cout << "[BALANCING] Case 1 (Uncle RED): Recoloring Node (" << k->parent->data 
                              << ") and Node (" << u->data << ") to BLACK, and Grandparent (" 
                              << k->parent->parent->data << ") to RED\n";
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                } else {
                    // Case 2 & 3: Uncle is BLACK
                    if (k == k->parent->right) {
                        k = k->parent;
                        std::cout << "[BALANCING] Case 2 (Triangle, Uncle BLACK): Inner child. Right-Left adjustment.\n";
                        leftRotate(k);
                    }
                    std::cout << "[BALANCING] Case 3 (Line, Uncle BLACK): Outer child. Recoloring Parent (" 
                              << k->parent->data << ") to BLACK, Grandparent (" 
                              << k->parent->parent->data << ") to RED.\n";
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    rightRotate(k->parent->parent);
                }
            }
            if (k == root) {
                break;
            }
        }
        root->color = BLACK; // Property 2: Root must be black
    }

    void inorderHelper(Node *node) const {
        if (node != TNULL) {
            inorderHelper(node->left);
            std::cout << node->data << " (" << (node->color == RED ? "RED" : "BLACK") << ")  ";
            inorderHelper(node->right);
        }
    }

public:
    RedBlackTree() {
        TNULL = new Node(0);
        TNULL->color = BLACK;
        TNULL->left = nullptr;
        TNULL->right = nullptr;
        root = TNULL;
        std::cout << "[INIT] Empty Red-Black Tree initialized with black leaf sentinel (TNULL).\n";
    }

    ~RedBlackTree() {
        // Simple cleanup can be added if needed, but omitted here for code clarity.
    }

    void insert(int key) {
        std::cout << "\n[INSERT START] Request to insert Key: " << key << "\n";
        Node *node = new Node(key);
        node->parent = nullptr;
        node->data = key;
        node->left = TNULL;
        node->right = TNULL;
        node->color = RED; // Rule 4: Newly inserted node is initially RED

        Node *y = nullptr;
        Node *x = this->root;

        // Standard BST Insertion
        while (x != TNULL) {
            y = x;
            if (node->data < x->data) {
                x = x->left;
            } else {
                x = x->right;
            }
        }

        node->parent = y;
        if (y == nullptr) {
            root = node;
            std::cout << " -> Key: " << key << " inserted as root node.\n";
        } else if (node->data < y->data) {
            y->left = node;
            std::cout << " -> Key: " << key << " placed as left child of Node (" << y->data << ").\n";
        } else {
            y->right = node;
            std::cout << " -> Key: " << key << " placed as right child of Node (" << y->data << ").\n";
        }

        if (node->parent == nullptr) {
            node->color = BLACK; // Root is black
            std::cout << " -> Root node recolored to BLACK.\n";
            return;
        }

        if (node->parent->parent == nullptr) {
            // Parent is root, no rotations needed
            return;
        }

        // Fulfill Red-Black properties
        fixInsert(node);
    }

    bool search(int key) const {
        std::cout << "\n[SEARCH] Searching for Key: " << key << "\n";
        Node *curr = root;
        int comparisons = 0;
        std::cout << " Path: ";
        while (curr != TNULL) {
            comparisons++;
            std::cout << "Node(" << curr->data << ") -> ";
            if (key == curr->data) {
                std::cout << "FOUND!\n";
                std::cout << " -> Searches took " << comparisons << " comparison(s).\n";
                return true;
            } else if (key < curr->data) {
                curr = curr->left;
            } else {
                curr = curr->right;
            }
        }
        std::cout << "TNULL (NOT FOUND)\n";
        std::cout << " -> Search failed after " << comparisons << " comparison(s).\n";
        return false;
    }

    void inorder() const {
        std::cout << "\n[TRAVERSAL] Inorder Traversal:\n ";
        inorderHelper(root);
        std::cout << "\n";
    }

    void printTreeHelper(Node *root, std::string indent, bool last) const {
        if (root != TNULL) {
            std::cout << indent;
            if (last) {
                std::cout << "R----";
                indent += "   ";
            } else {
                std::cout << "L----";
                indent += "|  ";
            }

            std::string sColor = (root->color == RED) ? "RED" : "BLACK";
            std::cout << root->data << "(" << sColor << ")" << std::endl;
            printTreeHelper(root->left, indent, false);
            printTreeHelper(root->right, indent, true);
        }
    }

    void printTree() const {
        std::cout << "\n------------------ TREE STRUCTURE ------------------\n";
        if (root == TNULL) {
            std::cout << "Empty Tree\n";
        } else {
            printTreeHelper(this->root, "", true);
        }
        std::cout << "----------------------------------------------------\n\n";
    }
};

int main() {
    std::cout << "===================================================\n";
    std::cout << "  Lab 5: Red-Black Tree Implementation & Balancing \n";
    std::cout << "===================================================\n";

    // Task 1: Tree Initialization
    RedBlackTree rbt;
    rbt.printTree();

    // Task 2 & 3: Node Insertion & Balancing (Rotations/Recolor)
    rbt.insert(10); // Root node
    rbt.printTree();

    rbt.insert(20); // Right child of 10
    rbt.printTree();

    rbt.insert(30); // Triggers Case 3: Uncle is BLACK (TNULL), line structure. Recoloring & Left Rotation.
    rbt.printTree();

    rbt.insert(15); // Left child of 30, triggers recoloring or adjustment.
    rbt.printTree();

    rbt.insert(25); // Insert to trigger further restructuring
    rbt.printTree();

    // Task 4: Search Operations
    rbt.search(15); // Existing key
    rbt.search(99); // Non-existing key

    // Task 5: Tree Traversal
    rbt.inorder();

    std::cout << "\n===================================================\n";
    std::cout << "  Lab 5 execution completed successfully!          \n";
    std::cout << "===================================================\n";

    return 0;
}
