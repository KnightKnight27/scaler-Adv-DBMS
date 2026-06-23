/**
 * Red-Black Tree - Complete Implementation in C++
 *
 * Properties of a Red-Black Tree:
 *  1. Every node is either RED or BLACK.
 *  2. The root is always BLACK.
 *  3. Every NULL leaf (NIL sentinel) is BLACK.
 *  4. If a node is RED, both its children must be BLACK.
 *  5. For every node, all simple paths from the node to descendant
 *     leaves contain the same number of BLACK nodes (black-height).
 */

#include <iostream>
#include <string>
#include <sstream>
#include <queue>
#include <iomanip>
#include <stdexcept>
#include <vector>

// ──────────────────────────────────────────────────────────────────────────────
// Color Enum
// ──────────────────────────────────────────────────────────────────────────────
enum Color { RED, BLACK };

// ──────────────────────────────────────────────────────────────────────────────
// Node Structure
// ──────────────────────────────────────────────────────────────────────────────
struct Node {
    int    data;
    Color  color;
    Node*  left;
    Node*  right;
    Node*  parent;

    explicit Node(int val)
        : data(val), color(RED),
          left(nullptr), right(nullptr), parent(nullptr) {}
};

// ──────────────────────────────────────────────────────────────────────────────
// Red-Black Tree Class
// ──────────────────────────────────────────────────────────────────────────────
class RedBlackTree {
private:
    Node* root;
    Node* NIL;   // Sentinel null-leaf node (always BLACK)

    // ── Internal helpers ──────────────────────────────────────────────────────

    /**
 \]    * createNIL()
     * Creates the sentinel NIL node shared by all leaf/null pointers.
     * It is permanently BLACK and its pointers point to itself to avoid
     * null-pointer dereferences in rotation/fixup routines.
     */
    Node* createNIL() {
        Node* nil = new Node(0);
        nil->color  = BLACK;
        nil->left   = nil;
        nil->right  = nil;
        nil->parent = nil;
        return nil;
    }

    // ── Rotations ─────────────────────────────────────────────────────────────

    /**
     * rotateLeft(x)
     * Performs a left rotation around node x.
     *
     *       x                  y
     *      / \      ==>       / \
     *     a   y             x   c
     *        / \           / \
     *       b   c         a   b
     *
     * Time: O(1)
     */
    void rotateLeft(Node* x) {
        Node* y  = x->right;       // y = x's right child
        x->right = y->left;        // y's left subtree becomes x's right subtree

        if (y->left != NIL)
            y->left->parent = x;

        y->parent = x->parent;     // link x's parent to y

        if (x->parent == NIL)
            root = y;              // x was root → y becomes root
        else if (x == x->parent->left)
            x->parent->left  = y;
        else
            x->parent->right = y;

        y->left   = x;             // place x on y's left
        x->parent = y;
    }

    /**
     * rotateRight(y)
     * Performs a right rotation around node y.
     *
     *         y              x
     *        / \    ==>     / \
     *       x   c          a   y
     *      / \                / \
     *     a   b              b   c
     *
     * Time: O(1)
     */
    void rotateRight(Node* y) {
        Node* x  = y->left;        // x = y's left child
        y->left  = x->right;       // x's right subtree becomes y's left subtree

        if (x->right != NIL)
            x->right->parent = y;

        x->parent = y->parent;     // link y's parent to x

        if (y->parent == NIL)
            root = x;              // y was root → x becomes root
        else if (y == y->parent->right)
            y->parent->right = x;
        else
            y->parent->left  = x;

        x->right  = y;             // place y on x's right
        y->parent = x;
    }

    // ── Insertion Fix-up ──────────────────────────────────────────────────────

    /**
     * insertFixup(z)
     * After a standard BST insert (node z is RED), restores RB-tree properties.
     *
     * There are three cases when z's parent is also RED (property 4 violated):
     *
     * Case 1 – Uncle is RED:
     *   Recolor parent & uncle to BLACK, grandparent to RED.
     *   Move z up to grandparent and repeat.
     *
     * Case 2 – Uncle is BLACK, z is an "inner" child (triangle):
     *   Rotate around parent to convert to Case 3.
     *
     * Case 3 – Uncle is BLACK, z is an "outer" child (line):
     *   Recolor parent & grandparent, then rotate around grandparent.
     *
     * Time: O(log n)
     */
    void insertFixup(Node* z) {
        while (z->parent->color == RED) {
            if (z->parent == z->parent->parent->left) {
                // Parent is a LEFT child ────────────────────────────────────
                Node* uncle = z->parent->parent->right;

                if (uncle->color == RED) {
                    // ── Case 1: Uncle is RED ──
                    z->parent->color          = BLACK;
                    uncle->color              = BLACK;
                    z->parent->parent->color  = RED;
                    z = z->parent->parent;   // move z up

                } else {
                    if (z == z->parent->right) {
                        // ── Case 2: z is inner (right) child → convert to Case 3 ──
                        z = z->parent;
                        rotateLeft(z);
                    }
                    // ── Case 3: z is outer (left) child ──
                    z->parent->color         = BLACK;
                    z->parent->parent->color = RED;
                    rotateRight(z->parent->parent);
                }

            } else {
                // Parent is a RIGHT child (mirror of the above) ───────────
                Node* uncle = z->parent->parent->left;

                if (uncle->color == RED) {
                    // ── Case 1 (mirror) ──
                    z->parent->color          = BLACK;
                    uncle->color              = BLACK;
                    z->parent->parent->color  = RED;
                    z = z->parent->parent;

                } else {
                    if (z == z->parent->left) {
                        // ── Case 2 (mirror) ──
                        z = z->parent;
                        rotateRight(z);
                    }
                    // ── Case 3 (mirror) ──
                    z->parent->color         = BLACK;
                    z->parent->parent->color = RED;
                    rotateLeft(z->parent->parent);
                }
            }
        }
        root->color = BLACK;   // Ensure root remains BLACK (property 2)
    }

    // ── Deletion helpers ──────────────────────────────────────────────────────

    /**
     * transplant(u, v)
     * Replaces the subtree rooted at u with the subtree rooted at v.
     * Does NOT update v's children — caller's responsibility.
     */
    void transplant(Node* u, Node* v) {
        if (u->parent == NIL)
            root = v;
        else if (u == u->parent->left)
            u->parent->left  = v;
        else
            u->parent->right = v;
        v->parent = u->parent;   // Always valid because v may be NIL sentinel
    }

    /**
     * minimum(x)
     * Returns the node with the smallest key in the subtree rooted at x.
     * Time: O(log n)
     */
    Node* minimum(Node* x) const {
        while (x->left != NIL)
            x = x->left;
        return x;
    }

    /**
     * maximum(x)
     * Returns the node with the largest key in the subtree rooted at x.
     * Time: O(log n)
     */
    Node* maximum(Node* x) const {
        while (x->right != NIL)
            x = x->right;
        return x;
    }

    /**
     * deleteFixup(x)
     * After removing a BLACK node, restores RB-tree properties.
     * x is the node that replaced the deleted node (may be NIL sentinel).
     *
     * Four cases (each mirrored for left/right):
     *
     * Case 1 – x's sibling w is RED:
     *   Recolor w & x's parent; rotate. Converts to Case 2, 3, or 4.
     *
     * Case 2 – w is BLACK, both of w's children are BLACK:
     *   Recolor w to RED; move "extra black" up to x's parent.
     *
     * Case 3 – w is BLACK, w's near child is RED, far child is BLACK:
     *   Recolor w & w's near child; rotate around w. Converts to Case 4.
     *
     * Case 4 – w is BLACK, w's far child is RED:
     *   Recolor; rotate around x's parent. Done.
     *
     * Time: O(log n)
     */
    void deleteFixup(Node* x) {
        while (x != root && x->color == BLACK) {
            if (x == x->parent->left) {
                Node* w = x->parent->right;    // w = sibling of x

                if (w->color == RED) {
                    // ── Case 1 ──
                    w->color           = BLACK;
                    x->parent->color   = RED;
                    rotateLeft(x->parent);
                    w = x->parent->right;
                }

                if (w->left->color == BLACK && w->right->color == BLACK) {
                    // ── Case 2 ──
                    w->color = RED;
                    x = x->parent;

                } else {
                    if (w->right->color == BLACK) {
                        // ── Case 3 ──
                        w->left->color = BLACK;
                        w->color       = RED;
                        rotateRight(w);
                        w = x->parent->right;
                    }
                    // ── Case 4 ──
                    w->color           = x->parent->color;
                    x->parent->color   = BLACK;
                    w->right->color    = BLACK;
                    rotateLeft(x->parent);
                    x = root;           // Done
                }

            } else {
                // Mirror: x is a right child ─────────────────────────────
                Node* w = x->parent->left;

                if (w->color == RED) {
                    // ── Case 1 (mirror) ──
                    w->color          = BLACK;
                    x->parent->color  = RED;
                    rotateRight(x->parent);
                    w = x->parent->left;
                }

                if (w->right->color == BLACK && w->left->color == BLACK) {
                    // ── Case 2 (mirror) ──
                    w->color = RED;
                    x = x->parent;

                } else {
                    if (w->left->color == BLACK) {
                        // ── Case 3 (mirror) ──
                        w->right->color = BLACK;
                        w->color        = RED;
                        rotateLeft(w);
                        w = x->parent->left;
                    }
                    // ── Case 4 (mirror) ──
                    w->color          = x->parent->color;
                    x->parent->color  = BLACK;
                    w->left->color    = BLACK;
                    rotateRight(x->parent);
                    x = root;
                }
            }
        }
        x->color = BLACK;   // Ensure x (possibly root or newly promoted node) is BLACK
    }

    // ── Traversals ────────────────────────────────────────────────────────────

    void inorderHelper(Node* node, std::vector<int>& result) const {
        if (node == NIL) return;
        inorderHelper(node->left,  result);
        result.push_back(node->data);
        inorderHelper(node->right, result);
    }

    void preorderHelper(Node* node, std::vector<int>& result) const {
        if (node == NIL) return;
        result.push_back(node->data);
        preorderHelper(node->left,  result);
        preorderHelper(node->right, result);
    }

    void postorderHelper(Node* node, std::vector<int>& result) const {
        if (node == NIL) return;
        postorderHelper(node->left,  result);
        postorderHelper(node->right, result);
        result.push_back(node->data);
    }

    // ── Validation helper ─────────────────────────────────────────────────────

    /**
     * Recursively validates RB-tree properties.
     * Returns the black-height of the subtree, or -1 on violation.
     */
    int validateHelper(Node* node) const {
        if (node == NIL) return 1;   // NIL sentinel counts as 1 black node

        // Property 4: RED node must have BLACK children
        if (node->color == RED) {
            if (node->left->color  == RED) return -1;
            if (node->right->color == RED) return -1;
        }

        int leftBH  = validateHelper(node->left);
        int rightBH = validateHelper(node->right);

        if (leftBH == -1 || rightBH == -1) return -1;

        // Property 5: Black-heights must be equal
        if (leftBH != rightBH) return -1;

        return leftBH + (node->color == BLACK ? 1 : 0);
    }

    // ── Pretty-print helper ───────────────────────────────────────────────────

    void printHelper(Node* node, const std::string& indent, bool isRight) const {
        if (node == NIL) return;

        std::cout << indent;
        std::cout << (isRight ? "R---- " : "L---- ");
        std::cout << node->data
                  << (node->color == RED ? "(R)" : "(B)")
                  << "\n";

        std::string childIndent = indent + (isRight ? "      " : "|     ");
        printHelper(node->left,  childIndent, false);
        printHelper(node->right, childIndent, true);
    }

    // ── Destructor helper ─────────────────────────────────────────────────────

    void destroyTree(Node* node) {
        if (node == NIL) return;
        destroyTree(node->left);
        destroyTree(node->right);
        delete node;
    }

public:
    // ── Constructor / Destructor ──────────────────────────────────────────────

    RedBlackTree() {
        NIL  = createNIL();
        root = NIL;
    }

    ~RedBlackTree() {
        destroyTree(root);
        delete NIL;
    }

    // ── Public Interface ──────────────────────────────────────────────────────

    /**
     * insert(key)
     * Inserts a new key into the tree.
     * Performs a standard BST insert then calls insertFixup().
     * Time: O(log n)
     */
    void insert(int key) {
        Node* z = new Node(key);
        z->left  = NIL;
        z->right = NIL;

        Node* y = NIL;   // future parent of z
        Node* x = root;

        // BST descent to find insertion position
        while (x != NIL) {
            y = x;
            if (z->data < x->data)
                x = x->left;
            else if (z->data > x->data)
                x = x->right;
            else {
                // Duplicate key — ignore (or handle as needed)
                delete z;
                return;
            }
        }

        z->parent = y;

        if (y == NIL)
            root = z;              // Tree was empty
        else if (z->data < y->data)
            y->left  = z;
        else
            y->right = z;

        // z starts RED; NIL children are already BLACK sentinel
        insertFixup(z);
    }

    /**
     * remove(key)
     * Removes the node with the given key from the tree.
     * Uses the standard RB-delete algorithm (Cormen et al., CLRS).
     * Time: O(log n)
     */
    void remove(int key) {
        Node* z = searchNode(root, key);
        if (z == NIL) {
            std::cerr << "Key " << key << " not found in tree.\n";
            return;
        }

        Node* y = z;                    // y = node to be spliced out
        Node* x;                        // x = node that moves into y's position
        Color yOriginalColor = y->color;

        if (z->left == NIL) {
            // Case A: z has no left child
            x = z->right;
            transplant(z, z->right);

        } else if (z->right == NIL) {
            // Case B: z has no right child
            x = z->left;
            transplant(z, z->left);

        } else {
            // Case C: z has two children → replace with in-order successor
            y = minimum(z->right);      // in-order successor
            yOriginalColor = y->color;
            x  = y->right;

            if (y->parent == z) {
                x->parent = y;          // Important for NIL sentinel
            } else {
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
            deleteFixup(x);    // Fix only when a BLACK node was removed
    }

    /**
     * search(key)
     * Returns true if the key exists in the tree, false otherwise.
     * Time: O(log n)
     */
    bool search(int key) const {
        return searchNode(root, key) != NIL;
    }

    /**
     * searchNode(node, key)
     * Returns the node with the given key, or NIL if not found.
     * Time: O(log n)
     */
    Node* searchNode(Node* node, int key) const {
        while (node != NIL) {
            if (key == node->data)
                return node;
            node = (key < node->data) ? node->left : node->right;
        }
        return NIL;
    }

    /**
     * getMin()
     * Returns the minimum key in the tree.
     * Throws std::runtime_error if the tree is empty.
     */
    int getMin() const {
        if (isEmpty()) throw std::runtime_error("Tree is empty.");
        return minimum(root)->data;
    }

    /**
     * getMax()
     * Returns the maximum key in the tree.
     * Throws std::runtime_error if the tree is empty.
     */
    int getMax() const {
        if (isEmpty()) throw std::runtime_error("Tree is empty.");
        return maximum(root)->data;
    }

    /**
     * successor(key)
     * Returns the in-order successor of the given key, or -1 if none exists.
     * Time: O(log n)
     */
    int successor(int key) const {
        Node* node = searchNode(root, key);
        if (node == NIL) return -1;

        if (node->right != NIL)
            return minimum(node->right)->data;

        Node* succ = node->parent;
        while (succ != NIL && node == succ->right) {
            node = succ;
            succ = succ->parent;
        }
        return (succ == NIL) ? -1 : succ->data;
    }

    /**
     * predecessor(key)
     * Returns the in-order predecessor of the given key, or -1 if none exists.
     * Time: O(log n)
     */
    int predecessor(int key) const {
        Node* node = searchNode(root, key);
        if (node == NIL) return -1;

        if (node->left != NIL)
            return maximum(node->left)->data;

        Node* pred = node->parent;
        while (pred != NIL && node == pred->left) {
            node = pred;
            pred = pred->parent;
        }
        return (pred == NIL) ? -1 : pred->data;
    }

    /**
     * height()
     * Returns the height of the tree (longest root-to-leaf path).
     * An empty tree has height -1.
     * Time: O(n)
     */
    int height() const {
        return heightHelper(root);
    }

    int heightHelper(Node* node) const {
        if (node == NIL) return -1;
        return 1 + std::max(heightHelper(node->left), heightHelper(node->right));
    }

    /**
     * blackHeight()
     * Returns the black-height of the tree
     * (number of BLACK nodes on any root-to-leaf path, not counting root).
     */
    int blackHeight() const {
        int bh = 0;
        Node* x = root;
        while (x != NIL) {
            if (x->color == BLACK) ++bh;
            x = x->left;
        }
        return bh;
    }

    /**
     * size()
     * Returns the total number of nodes in the tree.
     * Time: O(n)
     */
    int size() const {
        return sizeHelper(root);
    }

    int sizeHelper(Node* node) const {
        if (node == NIL) return 0;
        return 1 + sizeHelper(node->left) + sizeHelper(node->right);
    }

    /**
     * isEmpty()
     * Returns true if the tree contains no nodes.
     */
    bool isEmpty() const {
        return root == NIL;
    }

    // ── Traversals (public) ───────────────────────────────────────────────────

    std::vector<int> inorder() const {
        std::vector<int> result;
        inorderHelper(root, result);
        return result;
    }

    std::vector<int> preorder() const {
        std::vector<int> result;
        preorderHelper(root, result);
        return result;
    }

    std::vector<int> postorder() const {
        std::vector<int> result;
        postorderHelper(root, result);
        return result;
    }

    /**
     * levelOrder()
     * Returns nodes level by level (BFS order).
     * Time: O(n)
     */
    std::vector<int> levelOrder() const {
        std::vector<int> result;
        if (isEmpty()) return result;

        std::queue<Node*> q;
        q.push(root);
        while (!q.empty()) {
            Node* cur = q.front(); q.pop();
            result.push_back(cur->data);
            if (cur->left  != NIL) q.push(cur->left);
            if (cur->right != NIL) q.push(cur->right);
        }
        return result;
    }

    // ── Validation ────────────────────────────────────────────────────────────

    /**
     * isValidRBTree()
     * Verifies all five Red-Black Tree properties.
     * Returns true if the tree is a valid RB-tree.
     */
    bool isValidRBTree() const {
        if (root == NIL) return true;
        if (root->color != BLACK) return false;   // Property 2
        return validateHelper(root) != -1;
    }

    // ── Display ───────────────────────────────────────────────────────────────

    /**
     * printTree()
     * Prints a visual representation of the tree structure.
     * (R) = RED node, (B) = BLACK node.
     */
    void printTree() const {
        if (isEmpty()) {
            std::cout << "(empty tree)\n";
            return;
        }
        printHelper(root, "", true);
    }

    /**
     * printTraversals()
     * Convenience method to print all four traversals.
     */
    void printTraversals() const {
        auto printVec = [](const std::string& label, const std::vector<int>& v) {
            std::cout << label << ": ";
            for (int i = 0; i < (int)v.size(); ++i) {
                std::cout << v[i];
                if (i + 1 < (int)v.size()) std::cout << " -> ";
            }
            std::cout << "\n";
        };

        printVec("In-order   (sorted)", inorder());
        printVec("Pre-order           ", preorder());
        printVec("Post-order          ", postorder());
        printVec("Level-order (BFS)   ", levelOrder());
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Helper: print a separator line
// ──────────────────────────────────────────────────────────────────────────────
static void separator(const std::string& title = "") {
    if (title.empty()) {
        std::cout << std::string(60, '-') << "\n";
    } else {
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "  " << title << "\n";
        std::cout << std::string(60, '=') << "\n";
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// main() — Demonstration & Test Suite
// ──────────────────────────────────────────────────────────────────────────────
int main() {
    RedBlackTree rbt;

    // ── 1. Insertions ─────────────────────────────────────────────────────────
    separator("1. Insertions");
    std::vector<int> keys = {10, 20, 30, 15, 25, 5, 1, 7, 40, 35};
    std::cout << "Inserting: ";
    for (int k : keys) {
        std::cout << k << " ";
        rbt.insert(k);
    }
    std::cout << "\n\nTree structure after insertions:\n";
    rbt.printTree();

    std::cout << "\nTraversals:\n";
    rbt.printTraversals();

    std::cout << "\nSize        : " << rbt.size()        << "\n";
    std::cout << "Height      : " << rbt.height()       << "\n";
    std::cout << "Black-height: " << rbt.blackHeight()  << "\n";
    std::cout << "Min key     : " << rbt.getMin()       << "\n";
    std::cout << "Max key     : " << rbt.getMax()       << "\n";
    std::cout << "Valid RBT?  : " << (rbt.isValidRBTree() ? "YES" : "NO") << "\n";

    // ── 2. Search ─────────────────────────────────────────────────────────────
    separator("2. Search");
    for (int k : {15, 99, 5, 100}) {
        std::cout << "Search(" << std::setw(3) << k << ") = "
                  << (rbt.search(k) ? "FOUND" : "NOT FOUND") << "\n";
    }

    // ── 3. Successor / Predecessor ────────────────────────────────────────────
    separator("3. Successor & Predecessor");
    for (int k : {1, 10, 35, 40}) {
        int succ = rbt.successor(k);
        int pred = rbt.predecessor(k);
        std::cout << "Key " << std::setw(2) << k
                  << " -> predecessor: " << (pred == -1 ? std::string("none") : std::to_string(pred))
                  << ", successor: "   << (succ == -1 ? std::string("none") : std::to_string(succ))
                  << "\n";
    }

    // ── 4. Deletions ──────────────────────────────────────────────────────────
    separator("4. Deletions");
    std::vector<int> toDelete = {20, 1, 30};
    for (int k : toDelete) {
        std::cout << "\nDeleting " << k << "...\n";
        rbt.remove(k);
        std::cout << "Tree after delete(" << k << "):\n";
        rbt.printTree();
        std::cout << "Valid RBT? : " << (rbt.isValidRBTree() ? "YES" : "NO") << "\n";
        separator();
    }

    std::cout << "\nTraversals after deletions:\n";
    rbt.printTraversals();
    std::cout << "Size        : " << rbt.size()       << "\n";
    std::cout << "Height      : " << rbt.height()     << "\n";
    std::cout << "Valid RBT?  : " << (rbt.isValidRBTree() ? "YES" : "NO") << "\n";

    // ── 5. Edge cases ─────────────────────────────────────────────────────────
    separator("5. Edge Cases");

    // Duplicate insert
    std::cout << "Inserting duplicate 10 (should be ignored)...\n";
    rbt.insert(10);
    std::cout << "Size after duplicate insert: " << rbt.size() << " (unchanged)\n";

    // Delete non-existent key
    std::cout << "\nDeleting non-existent key 999...\n";
    rbt.remove(999);

    // Clear the tree
    std::cout << "\nDeleting all remaining keys...\n";
    for (int k : rbt.inorder()) rbt.remove(k);   // copy first — can't iterate while removing
    // Actually call correctly:
    while (!rbt.isEmpty()) {
        rbt.remove(rbt.getMin());
    }
    std::cout << "Tree empty? : " << (rbt.isEmpty() ? "YES" : "NO") << "\n";
    rbt.printTree();

    // ── 6. Large insertion test ───────────────────────────────────────────────
    separator("6. Large Insertion Test (1..20)");
    for (int i = 1; i <= 20; ++i) rbt.insert(i);
    std::cout << "Inserted 1 through 20.\n";
    std::cout << "Size        : " << rbt.size()        << "\n";
    std::cout << "Height      : " << rbt.height()      << "\n";
    std::cout << "Black-height: " << rbt.blackHeight() << "\n";
    std::cout << "Valid RBT?  : " << (rbt.isValidRBTree() ? "YES" : "NO") << "\n";
    std::cout << "\nTree structure:\n";
    rbt.printTree();

    return 0;
}