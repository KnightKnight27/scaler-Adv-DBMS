#include "RedBlackTree.h"

// Constructor: Init sentinel node `nil` colored BLACK and point root to it
RedBlackTree::RedBlackTree() {
    nil = new Node(0);
    nil->color = BLACK;
    nil->left = nil;
    nil->right = nil;
    nil->parent = nullptr;
    root = nil;
}

// Helper to destroy tree recursively and free up memory
void RedBlackTree::destroyTree(Node* node) {
    if (node != nil && node != nullptr) {
        destroyTree(node->left);
        destroyTree(node->right);
        delete node;
    }
}

// Destructor: Clean up all dynamically allocated memory
RedBlackTree::~RedBlackTree() {
    destroyTree(root);
    delete nil;
}

// Left Rotate around node x
void RedBlackTree::leftRotate(Node* x) {
    // y is x's right child
    Node* y = x->right;
    
    // Move y's left subtree to x's right subtree
    x->right = y->left;
    if (y->left != nil) {
        y->left->parent = x;
    }
    
    // Update y's parent
    y->parent = x->parent;
    if (x->parent == nullptr) {
        root = y; // x was root
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    
    // Put x on y's left
    y->left = x;
    x->parent = y;
}

// Right Rotate around node y
void RedBlackTree::rightRotate(Node* y) {
    // x is y's left child
    Node* x = y->left;
    
    // Move x's right subtree to y's left subtree
    y->left = x->right;
    if (x->right != nil) {
        x->right->parent = y;
    }
    
    // Update x's parent
    x->parent = y->parent;
    if (y->parent == nullptr) {
        root = x; // y was root
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    
    // Put y on x's right
    x->right = y;
    y->parent = x;
}

// BST Insertion followed by RBT self-balancing
void RedBlackTree::insert(int key) {
    Node* z = new Node(key);
    z->left = nil;
    z->right = nil;
    z->color = RED; // All newly inserted nodes are colored RED by default

    Node* y = nullptr;
    Node* x = root;

    // Traverse down the tree to find the insertion point
    while (x != nil) {
        y = x;
        if (z->data < x->data) {
            x = x->left;
        } else {
            x = x->right;
        }
    }

    // Assign parent to the new node
    z->parent = y;

    if (y == nullptr) {
        root = z; // Tree was empty
    } else if (z->data < y->data) {
        y->left = z;
    } else {
        y->right = z;
    }

    // If new node is the root, color it black and return
    if (z->parent == nullptr) {
        z->color = BLACK;
        return;
    }

    // If grandparent does not exist, parent is root (already BLACK), no violations
    if (z->parent->parent == nullptr) {
        return;
    }

    // Fix violations of Red-Black Tree invariants
    fixInsert(z);
}

// Restore Red-Black Tree invariants after insertion
void RedBlackTree::fixInsert(Node* k) {
    Node* u;
    // Repeat until parent becomes BLACK or we reach the root
    while (k->parent->color == RED) {
        // Case A: Parent of k is a LEFT child of Grandparent
        if (k->parent == k->parent->parent->left) {
            u = k->parent->parent->right; // Uncle node

            // Case 1: Uncle is RED
            if (u->color == RED) {
                k->parent->color = BLACK;
                u->color = BLACK;
                k->parent->parent->color = RED;
                k = k->parent->parent; // Move check pointer up to Grandparent
            }
            // Uncle is BLACK
            else {
                // Case 2: k forms a triangle (k is a right child)
                if (k == k->parent->right) {
                    k = k->parent;
                    leftRotate(k); // Convert to Case 3 (line)
                }
                
                // Case 3: k forms a line (k is a left child)
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                rightRotate(k->parent->parent);
            }
        }
        // Case B: Parent of k is a RIGHT child of Grandparent
        else {
            u = k->parent->parent->left; // Uncle node

            // Case 1: Uncle is RED
            if (u->color == RED) {
                k->parent->color = BLACK;
                u->color = BLACK;
                k->parent->parent->color = RED;
                k = k->parent->parent; // Move check pointer up to Grandparent
            }
            // Uncle is BLACK
            else {
                // Case 2: k forms a triangle (k is a left child)
                if (k == k->parent->left) {
                    k = k->parent;
                    rightRotate(k); // Convert to Case 3 (line)
                }
                
                // Case 3: k forms a line (k is a right child)
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                leftRotate(k->parent->parent);
            }
        }
        
        if (k == root) {
            break;
        }
    }
    
    // Invariant 2: Root is always BLACK
    root->color = BLACK;
}

// Inorder Traversal Helper
void RedBlackTree::inorderHelper(Node* node) const {
    if (node != nil) {
        inorderHelper(node->left);
        std::cout << node->data << " ";
        inorderHelper(node->right);
    }
}

// Inorder Traversal API
void RedBlackTree::inorder() const {
    inorderHelper(root);
    std::cout << std::endl;
}

// Visual Print Helper: Generates a beautiful hierarchical representation
void RedBlackTree::printTreeHelper(Node* node, const std::string& indent, bool last) const {
    if (node != nil) {
        std::cout << indent;
        if (last) {
            std::cout << "└── ";
        } else {
            std::cout << "├── ";
        }
        
        // Output colored representations: RED using bold red ANSI escape codes, BLACK using default/grey
        std::string colorCode = (node->color == RED) ? "\033[1;31m" : "\033[1;30m";
        std::string colorStr = (node->color == RED) ? "RED" : "BLACK";
        std::string resetCode = "\033[0m";
        
        std::cout << node->data << " (" << colorCode << colorStr << resetCode << ")" << std::endl;
        
        // Recursively print subtrees
        std::string newIndent = indent + (last ? "    " : "│   ");
        printTreeHelper(node->left, newIndent, false);
        printTreeHelper(node->right, newIndent, true);
    }
}

// Beautiful hierarchical tree printout
void RedBlackTree::printTree() const {
    if (root == nil) {
        std::cout << "(Empty Tree)" << std::endl;
    } else {
        printTreeHelper(root, "", true);
    }
}

// Get the root node
Node* RedBlackTree::getRoot() const {
    return root;
}

// Check if node is the nil sentinel
bool RedBlackTree::isNil(Node* node) const {
    return node == nil;
}
