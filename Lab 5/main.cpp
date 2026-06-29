#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <iomanip>

enum Color { RED, BLACK };

class RedBlackTree {
private:
    struct Node {
        int data;
        Color color;
        Node* left;
        Node* right;
        Node* parent;

        Node(int val) : data(val), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
    };

    Node* root;
    Node* NIL;

    void leftRotate(Node* x) {
        std::cout << "    [Left Rotate] Pivot: " << x->data << ", Right child: " << x->right->data << std::endl;
        Node* y = x->right;
        x->right = y->left;
        if (y->left != NIL) {
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

    void rightRotate(Node* y) {
        std::cout << "    [Right Rotate] Pivot: " << y->data << ", Left child: " << y->left->data << std::endl;
        Node* x = y->left;
        y->left = x->right;
        if (x->right != NIL) {
            x->right->parent = y;
        }
        x->parent = y->parent;
        if (y->parent == nullptr) {
            this->root = x;
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
        while (k->parent->color == RED) {
            std::cout << "  [Fixup Loop] Node " << k->data << " (RED) has parent " << k->parent->data << " (RED)." << std::endl;
            if (k->parent == k->parent->parent->right) {
                u = k->parent->parent->left; // uncle
                std::cout << "  Parent is RIGHT child of grandparent " << k->parent->parent->data << ". Uncle is " 
                          << (u == NIL ? "NIL (BLACK)" : (u->color == RED ? std::to_string(u->data) + " (RED)" : std::to_string(u->data) + " (BLACK)")) << std::endl;
                
                if (u->color == RED) {
                    // Case 1: Uncle is RED
                    std::cout << "  ==> Case 1: Uncle is RED. Recoloring parent, uncle, and grandparent." << std::endl;
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                    std::cout << "  Grandparent " << k->data << " is now RED. Moving pointer up to it." << std::endl;
                } else {
                    if (k == k->parent->left) {
                        // Case 2: Uncle is BLACK, k is left child (Triangle)
                        std::cout << "  ==> Case 2: Uncle is BLACK, node is LEFT child. Rotating RIGHT on parent " << k->parent->data << "." << std::endl;
                        k = k->parent;
                        rightRotate(k);
                    }
                    // Case 3: Uncle is BLACK, k is right child (Line)
                    std::cout << "  ==> Case 3: Uncle is BLACK, node is RIGHT child. Recoloring parent " << k->parent->data 
                              << " (BLACK) and grandparent " << k->parent->parent->data << " (RED), then rotating LEFT on grandparent." << std::endl;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    leftRotate(k->parent->parent);
                }
            } else {
                u = k->parent->parent->right; // uncle
                std::cout << "  Parent is LEFT child of grandparent " << k->parent->parent->data << ". Uncle is " 
                          << (u == NIL ? "NIL (BLACK)" : (u->color == RED ? std::to_string(u->data) + " (RED)" : std::to_string(u->data) + " (BLACK)")) << std::endl;
                
                if (u->color == RED) {
                    // Case 1: Uncle is RED
                    std::cout << "  ==> Case 1: Uncle is RED. Recoloring parent, uncle, and grandparent." << std::endl;
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                    std::cout << "  Grandparent " << k->data << " is now RED. Moving pointer up to it." << std::endl;
                } else {
                    if (k == k->parent->right) {
                        // Case 2: Uncle is BLACK, k is right child (Triangle)
                        std::cout << "  ==> Case 2: Uncle is BLACK, node is RIGHT child. Rotating LEFT on parent " << k->parent->data << "." << std::endl;
                        k = k->parent;
                        leftRotate(k);
                    }
                    // Case 3: Uncle is BLACK, k is left child (Line)
                    std::cout << "  ==> Case 3: Uncle is BLACK, node is LEFT child. Recoloring parent " << k->parent->data 
                              << " (BLACK) and grandparent " << k->parent->parent->data << " (RED), then rotating RIGHT on grandparent." << std::endl;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    rightRotate(k->parent->parent);
                }
            }
            if (k == root) {
                std::cout << "  Reached root. Exiting loop." << std::endl;
                break;
            }
        }
        if (root->color == RED) {
            std::cout << "  Ensuring root is BLACK." << std::endl;
            root->color = BLACK;
        }
    }

    void printHelper(Node* root, std::string indent, bool last) const {
        if (root != NIL) {
            std::cout << indent;
            if (last) {
                std::cout << "└── ";
                indent += "    ";
            } else {
                std::cout << "├── ";
                indent += "│   ";
            }

            // Print with colors if terminal supports it
            std::string colorStr = (root->color == RED) ? "\033[1;31m[RED]\033[0m" : "[BLACK]";
            std::cout << root->data << " " << colorStr << std::endl;

            printHelper(root->left, indent, false);
            printHelper(root->right, indent, true);
        }
    }

    void inorderHelper(Node* node, std::vector<std::string>& result) const {
        if (node != NIL) {
            inorderHelper(node->left, result);
            std::string colorStr = (node->color == RED) ? "RED" : "BLACK";
            result.push_back(std::to_string(node->data) + "(" + colorStr + ")");
            inorderHelper(node->right, result);
        }
    }

    bool verifyNoRedRed(Node* node) {
        if (node == NIL) return true;
        if (node->color == RED) {
            if (node->left->color == RED || node->right->color == RED) {
                return false;
            }
        }
        return verifyNoRedRed(node->left) && verifyNoRedRed(node->right);
    }

    int getBlackHeightAndVerify(Node* node, bool& isValid) {
        if (node == NIL) {
            return 1; // NIL is BLACK
        }
        int leftHeight = getBlackHeightAndVerify(node->left, isValid);
        int rightHeight = getBlackHeightAndVerify(node->right, isValid);
        if (leftHeight != rightHeight) {
            isValid = false;
        }
        return leftHeight + (node->color == BLACK ? 1 : 0);
    }

    bool verifyBST(Node* node, int minVal, int maxVal) {
        if (node == NIL) return true;
        if (node->data <= minVal || node->data >= maxVal) return false;
        return verifyBST(node->left, minVal, node->data) && verifyBST(node->right, node->data, maxVal);
    }

    void destroyTree(Node* node) {
        if (node != NIL) {
            destroyTree(node->left);
            destroyTree(node->right);
            delete node;
        }
    }

public:
    RedBlackTree() {
        NIL = new Node(0);
        NIL->color = BLACK;
        NIL->left = nullptr;
        NIL->right = nullptr;
        NIL->parent = nullptr;
        root = NIL;
    }

    ~RedBlackTree() {
        destroyTree(root);
        delete NIL;
    }

    void insert(int key) {
        std::cout << "\n============================================\n";
        std::cout << ">>> INSERTING KEY: " << key << "\n";
        std::cout << "============================================\n";

        Node* node = new Node(key);
        node->parent = nullptr;
        node->left = NIL;
        node->right = NIL;
        node->color = RED;

        Node* y = nullptr;
        Node* x = this->root;

        std::cout << "BST Search path for insertion: ";
        if (x == NIL) {
            std::cout << "Tree is empty. Node becomes root.";
        }
        while (x != NIL) {
            y = x;
            std::cout << x->data << " -> ";
            if (node->data < x->data) {
                x = x->left;
            } else {
                x = x->right;
            }
        }
        std::cout << "NIL" << std::endl;

        node->parent = y;
        if (y == nullptr) {
            root = node;
            std::cout << "Root node colored BLACK automatically." << std::endl;
            root->color = BLACK;
        } else if (node->data < y->data) {
            y->left = node;
            std::cout << "Placed " << key << " as LEFT child of " << y->data << std::endl;
        } else {
            y->right = node;
            std::cout << "Placed " << key << " as RIGHT child of " << y->data << std::endl;
        }

        std::cout << "\nTree state BEFORE fixup:" << std::endl;
        printTree();

        if (node->parent == nullptr) {
            std::cout << "No fixup needed (root insertion)." << std::endl;
        } else if (node->parent->parent == nullptr) {
            std::cout << "No fixup needed (parent is root, which is BLACK)." << std::endl;
        } else {
            std::cout << "Starting insertFixup..." << std::endl;
            insertFixup(node);
        }

        std::cout << "\nTree state AFTER insertion & fixup:" << std::endl;
        printTree();

        verifyAndPrintProperties();
    }

    bool search(int key) {
        std::cout << "\n============================================\n";
        std::cout << ">>> SEARCHING FOR KEY: " << key << "\n";
        std::cout << "============================================\n";
        Node* x = this->root;
        int comparisons = 0;
        std::vector<std::string> path;

        while (x != NIL) {
            comparisons++;
            std::string colorStr = (x->color == RED) ? "RED" : "BLACK";
            path.push_back(std::to_string(x->data) + "(" + colorStr + ")");

            if (key == x->data) {
                std::cout << "Search Path: ";
                for (size_t i = 0; i < path.size(); ++i) {
                    std::cout << path[i] << (i == path.size() - 1 ? "" : " -> ");
                }
                std::cout << std::endl;
                std::cout << "Result: FOUND!" << std::endl;
                std::cout << "Total Comparisons: " << comparisons << std::endl;
                return true;
            }

            if (key < x->data) {
                x = x->left;
            } else {
                x = x->right;
            }
        }

        path.push_back("NIL(BLACK)");
        std::cout << "Search Path: ";
        for (size_t i = 0; i < path.size(); ++i) {
            std::cout << path[i] << (i == path.size() - 1 ? "" : " -> ");
        }
        std::cout << std::endl;
        std::cout << "Result: NOT FOUND!" << std::endl;
        std::cout << "Total Comparisons: " << comparisons << " (including NIL check)" << std::endl;
        return false;
    }

    void printTree() const {
        if (root == NIL) {
            std::cout << "[Empty Tree]" << std::endl;
            return;
        }
        printHelper(root, "", true);
    }

    void printInorder() const {
        std::cout << "\nInorder Traversal: ";
        if (root == NIL) {
            std::cout << "[Empty Tree]" << std::endl;
            return;
        }
        std::vector<std::string> nodes;
        inorderHelper(root, nodes);
        for (size_t i = 0; i < nodes.size(); ++i) {
            std::cout << nodes[i] << (i == nodes.size() - 1 ? "" : ", ");
        }
        std::cout << std::endl;
    }

    void verifyAndPrintProperties() {
        std::cout << "\n=== Programmatic Red-Black Tree Property Verification ===" << std::endl;
        bool allOk = true;

        // Property 1: Nodes are either RED or BLACK (satisfied by enum design)
        std::cout << "  1. Node Color Enum Constraints: PASSED (By Design)" << std::endl;

        // Property 2: Root is BLACK (or tree is empty)
        if (root != NIL && root->color != BLACK) {
            std::cout << "  2. Root color is BLACK: FAILED (Root is RED!)" << std::endl;
            allOk = false;
        } else {
            std::cout << "  2. Root color is BLACK: PASSED" << std::endl;
        }

        // Property 3: Every leaf (NIL) is BLACK (satisfied by NIL definition)
        std::cout << "  3. NIL Leaf color is BLACK: PASSED" << std::endl;

        // Property 4: If a node is RED, then both its children are BLACK.
        bool noRedRed = verifyNoRedRed(root);
        if (!noRedRed) {
            std::cout << "  4. No two consecutive RED nodes: FAILED!" << std::endl;
            allOk = false;
        } else {
            std::cout << "  4. No two consecutive RED nodes: PASSED" << std::endl;
        }

        // Property 5: Simple paths from root to NIL have the same number of BLACK nodes.
        bool blackHeightValid = true;
        int bh = getBlackHeightAndVerify(root, blackHeightValid);
        if (!blackHeightValid) {
            std::cout << "  5. Equal black height on all paths: FAILED!" << std::endl;
            allOk = false;
        } else {
            std::cout << "  5. Equal black height on all paths: PASSED (Black Height = " << bh << ")" << std::endl;
        }

        // BST Ordering verification:
        bool isBST = verifyBST(root, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        if (!isBST) {
            std::cout << "  6. Binary Search Tree Ordering: FAILED!" << std::endl;
            allOk = false;
        } else {
            std::cout << "  6. Binary Search Tree Ordering: PASSED" << std::endl;
        }

        if (allOk) {
            std::cout << ">>> VERIFICATION RESULT: SUCCESS! All Red-Black Tree properties are satisfied." << std::endl;
        } else {
            std::cout << ">>> VERIFICATION RESULT: FAILED! Tree violates one or more properties." << std::endl;
        }
    }
};

int main() {
    std::cout << "=========================================================\n";
    std::cout << "            Red-Black Tree Lab Demonstration             \n";
    std::cout << "=========================================================\n";

    // Task 1: Tree Initialization
    std::cout << "\n[Task 1] Tree Initialization" << std::endl;
    RedBlackTree rbt;
    std::cout << "Initialized empty Red-Black Tree." << std::endl;
    std::cout << "Initial Tree State:" << std::endl;
    rbt.printTree();
    rbt.verifyAndPrintProperties();

    // Task 2, 3, 6: Node Insertion & Balancing & Property Verification
    std::cout << "\n[Task 2, 3, 6] Node Insertion, Balancing, and Property Verification" << std::endl;
    std::vector<int> valuesToInsert = {10, 20, 30, 15, 25, 5, 1};
    for (int val : valuesToInsert) {
        rbt.insert(val);
    }

    // Task 4: Search Operations
    std::cout << "\n[Task 4] Search Operations" << std::endl;
    // Search existing values
    rbt.search(15);
    rbt.search(30);
    rbt.search(5);
    
    // Search non-existing values
    rbt.search(100);
    rbt.search(0);

    // Task 5: Tree Traversal
    std::cout << "\n[Task 5] Tree Traversal" << std::endl;
    rbt.printInorder();

    std::cout << "\n=========================================================\n";
    std::cout << "        Red-Black Tree Lab Demonstration Finished        \n";
    std::cout << "=========================================================\n";
    return 0;
}
