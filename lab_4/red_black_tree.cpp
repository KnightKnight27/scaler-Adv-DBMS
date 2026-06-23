#include <iostream>

/**
 * Red-Black Tree Implementation
 * 
 * Properties:
 * 1. Every node is either RED or BLACK
 * 2. Root is always BLACK
 * 3. No two consecutive RED nodes (RED node's parent must be BLACK)
 * 4. Every path from root to NULL has same number of BLACK nodes
 * 
 * These properties guarantee O(log n) height
 */

enum Color { RED, BLACK };

struct RBNode {
    int     key;
    Color   color;
    RBNode *left, *right, *parent;

    explicit RBNode(int k)
        : key(k), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
private:
    RBNode* root = nullptr;

    /**
     * left_rotate: Rotate left around node x
     *     x              y
     *    / \            / \
     *   A   y    =>    x   C
     *      / \        / \
     *     B   C      A   B
     */
    void left_rotate(RBNode* x) {
        RBNode* y = x->right;
        x->right = y->left;
        
        if (y->left)
            y->left->parent = x;
        
        y->parent = x->parent;
        
        if (!x->parent)
            root = y;
        else if (x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;
        
        y->left = x;
        x->parent = y;
    }

    /**
     * right_rotate: Rotate right around node x
     *       x          y
     *      / \        / \
     *     y   C  =>  A   x
     *    / \            / \
     *   A   B          B   C
     */
    void right_rotate(RBNode* x) {
        RBNode* y = x->left;
        x->left = y->right;
        
        if (y->right)
            y->right->parent = x;
        
        y->parent = x->parent;
        
        if (!x->parent)
            root = y;
        else if (x == x->parent->right)
            x->parent->right = y;
        else
            x->parent->left = y;
        
        y->right = x;
        x->parent = y;
    }

    /**
     * fix_insert: Restore RB properties after insertion
     * 
     * Cases:
     * 1. Uncle is RED: Recolor parent, uncle, grandparent
     * 2. Uncle is BLACK and node is right child: Left-rotate parent
     * 3. Uncle is BLACK and node is left child: Right-rotate grandparent
     */
    void fix_insert(RBNode* z) {
        while (z->parent && z->parent->color == RED) {
            RBNode* gp = z->parent->parent;
            
            if (z->parent == gp->left) {
                RBNode* uncle = gp->right;
                
                // Case 1: Uncle is RED
                if (uncle && uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;
                    z = gp;
                } else {
                    // Case 2: Node is right child
                    if (z == z->parent->right) {
                        z = z->parent;
                        left_rotate(z);
                    }
                    // Case 3: Node is left child
                    z->parent->color = BLACK;
                    gp->color = RED;
                    right_rotate(gp);
                }
            } else {
                // Mirror cases (parent is right child of grandparent)
                RBNode* uncle = gp->left;
                
                if (uncle && uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;
                    z = gp;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        right_rotate(z);
                    }
                    z->parent->color = BLACK;
                    gp->color = RED;
                    left_rotate(gp);
                }
            }
        }
        root->color = BLACK;  // Root must always be BLACK
    }

    /**
     * transplant: Replace subtree rooted at u with subtree rooted at v
     */
    void transplant(RBNode* u, RBNode* v) {
        if (!u->parent)
            root = v;
        else if (u == u->parent->left)
            u->parent->left = v;
        else
            u->parent->right = v;
        
        if (v)
            v->parent = u->parent;
    }

    /**
     * minimum: Find minimum node in subtree
     */
    RBNode* minimum(RBNode* node) {
        while (node->left)
            node = node->left;
        return node;
    }

    /**
     * fix_delete: Restore RB properties after deletion
     */
    void fix_delete(RBNode* x, RBNode* x_parent) {
        while (x != root && (!x || x->color == BLACK)) {
            if (x == (x_parent ? x_parent->left : nullptr)) {
                RBNode* w = x_parent->right;
                
                // Case 1: Sibling is RED
                if (w && w->color == RED) {
                    w->color = BLACK;
                    x_parent->color = RED;
                    left_rotate(x_parent);
                    w = x_parent->right;
                }
                
                // Case 2: Both of sibling's children are BLACK
                if ((!w->left || w->left->color == BLACK) &&
                    (!w->right || w->right->color == BLACK)) {
                    if (w) w->color = RED;
                    x = x_parent;
                    x_parent = x->parent;
                } else {
                    // Case 3: Sibling's right child is BLACK
                    if (!w->right || w->right->color == BLACK) {
                        if (w->left) w->left->color = BLACK;
                        w->color = RED;
                        right_rotate(w);
                        w = x_parent->right;
                    }
                    // Case 4: Sibling's right child is RED
                    w->color = x_parent->color;
                    x_parent->color = BLACK;
                    if (w->right) w->right->color = BLACK;
                    left_rotate(x_parent);
                    x = root;
                }
            } else {
                // Mirror cases
                RBNode* w = x_parent->left;
                
                if (w && w->color == RED) {
                    w->color = BLACK;
                    x_parent->color = RED;
                    right_rotate(x_parent);
                    w = x_parent->left;
                }
                
                if ((!w->right || w->right->color == BLACK) &&
                    (!w->left || w->left->color == BLACK)) {
                    if (w) w->color = RED;
                    x = x_parent;
                    x_parent = x->parent;
                } else {
                    if (!w->left || w->left->color == BLACK) {
                        if (w->right) w->right->color = BLACK;
                        w->color = RED;
                        left_rotate(w);
                        w = x_parent->left;
                    }
                    w->color = x_parent->color;
                    x_parent->color = BLACK;
                    if (w->left) w->left->color = BLACK;
                    right_rotate(x_parent);
                    x = root;
                }
            }
        }
        if (x) x->color = BLACK;
    }

    /**
     * inorder: Inorder traversal (prints sorted keys)
     */
    void inorder(RBNode* node) const {
        if (!node) return;
        inorder(node->left);
        std::cout << node->key << (node->color == RED ? "R" : "B") << " ";
        inorder(node->right);
    }

public:
    /**
     * insert: Insert a new key into the Red-Black Tree
     */
    void insert(int key) {
        RBNode* z = new RBNode(key);
        RBNode* y = nullptr;
        RBNode* x = root;
        
        // Standard BST insert
        while (x) {
            y = x;
            x = (z->key < x->key) ? x->left : x->right;
        }
        
        z->parent = y;
        
        if (!y)
            root = z;
        else if (z->key < y->key)
            y->left = z;
        else
            y->right = z;
        
        // Fix RB properties
        fix_insert(z);
    }

    /**
     * remove: Delete a key from the Red-Black Tree
     */
    void remove(int key) {
        RBNode* z = root;
        
        // Find node to delete
        while (z && z->key != key)
            z = (key < z->key) ? z->left : z->right;
        
        if (!z) return;  // Key not found
        
        RBNode* y = z;
        RBNode* x = nullptr;
        RBNode* x_parent = nullptr;
        Color y_original_color = y->color;
        
        if (!z->left) {
            x = z->right;
            x_parent = z->parent;
            transplant(z, z->right);
        } else if (!z->right) {
            x = z->left;
            x_parent = z->parent;
            transplant(z, z->left);
        } else {
            // Node has two children
            y = minimum(z->right);
            y_original_color = y->color;
            x = y->right;
            
            if (y->parent == z) {
                x_parent = y;
            } else {
                x_parent = y->parent;
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
        
        if (y_original_color == BLACK)
            fix_delete(x, x_parent);
    }

    /**
     * print: Print tree in inorder (sorted)
     */
    void print() const {
        inorder(root);
        std::cout << "\n";
    }
    
    /**
     * search: Check if key exists in tree
     */
    bool search(int key) const {
        RBNode* node = root;
        while (node) {
            if (key == node->key) return true;
            node = (key < node->key) ? node->left : node->right;
        }
        return false;
    }
};


/**
 * Test Red-Black Tree implementation
 */
int main() {
    std::cout << "=== Red-Black Tree Implementation ===" << std::endl;
    std::cout << "In-memory self-balancing BST" << std::endl << std::endl;
    
    RedBlackTree rbt;
    
    // Test 1: Insert sequence from lab spec
    std::cout << "Test 1: Insert sequence {10, 20, 30, 15, 25, 5, 1}" << std::endl;
    int keys[] = {10, 20, 30, 15, 25, 5, 1};
    
    for (int k : keys) {
        std::cout << "Inserting " << k << "..." << std::endl;
        rbt.insert(k);
    }
    
    std::cout << "\nInorder traversal (key + color R/B):" << std::endl;
    rbt.print();
    
    // Test 2: Search
    std::cout << "\nTest 2: Search operations" << std::endl;
    int search_keys[] = {15, 99, 5, 100};
    for (int k : search_keys) {
        std::cout << "Search " << k << ": " 
                  << (rbt.search(k) ? "FOUND" : "NOT FOUND") << std::endl;
    }
    
    // Test 3: Delete
    std::cout << "\nTest 3: Delete operation" << std::endl;
    std::cout << "Deleting 20..." << std::endl;
    rbt.remove(20);
    
    std::cout << "Inorder after deleting 20:" << std::endl;
    rbt.print();
    
    std::cout << "Deleting 5..." << std::endl;
    rbt.remove(5);
    
    std::cout << "Inorder after deleting 5:" << std::endl;
    rbt.print();
    
    // Test 4: Larger sequence
    std::cout << "\nTest 4: Larger insertion sequence" << std::endl;
    RedBlackTree rbt2;
    std::cout << "Inserting 1-15..." << std::endl;
    for (int i = 1; i <= 15; i++) {
        rbt2.insert(i);
    }
    
    std::cout << "Inorder traversal:" << std::endl;
    rbt2.print();
    
    std::cout << "\n✓ Red-Black Tree tests complete!" << std::endl;
    
    return 0;
}
