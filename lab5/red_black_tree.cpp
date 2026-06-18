#include <iostream>
#include <string>
using namespace std;

enum Color { RED, BLACK };

struct Node {
    int   key;
    Color color;
    Node *left, *right, *parent;

    explicit Node(int k)
        : key(k), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
public:
    RedBlackTree() {
        NIL         = new Node(0);
        NIL->color  = BLACK;
        NIL->left   = NIL->right = NIL->parent = NIL;
        root        = NIL;
    }

    // Task 2: Insert a key and restore RB properties.
    void insert(int key) {
        Node* z  = new Node(key);
        z->left  = NIL;
        z->right = NIL;

        Node* y = NIL;
        Node* x = root;
        while (x != NIL) {
            y = x;
            x = (z->key < x->key) ? x->left : x->right;
        }
        z->parent = y;
        if (y == NIL)          root     = z;
        else if (z->key < y->key) y->left  = z;
        else                       y->right = z;

        cout << "[INSERT] Key " << key << " inserted as RED\n";
        fixInsert(z);
    }

    // Task 4: Search for a key.
    bool search(int key) const {
        Node* n = searchHelper(root, key);
        if (n != NIL) {
            cout << "[FOUND]     Key=" << key
                 << "  Color=" << colorStr(n->color) << "\n";
            return true;
        }
        cout << "[NOT FOUND] Key=" << key << "\n";
        return false;
    }

    // Task 5: Inorder traversal (produces sorted output).
    void inorder() const {
        cout << "Inorder: ";
        inorderHelper(root);
        cout << "\n";
    }

    // Task 6: Verify all Red-Black Tree properties.
    bool verifyProperties() const {
        if (root == NIL) { cout << "[VALID] Empty tree\n"; return true; }
        if (root->color != BLACK) {
            cout << "[INVALID] Root must be BLACK\n";
            return false;
        }
        int bh = 0;
        bool ok = verifyHelper(root, bh);
        cout << "[VERIFY] Tree is " << (ok ? "VALID" : "INVALID")
             << "  Black-height=" << bh << "\n";
        return ok;
    }

private:
    Node* root;
    Node* NIL;   // sentinel

    static string colorStr(Color c) { return c == RED ? "RED" : "BLACK"; }

    // ── Rotations ─────────────────────────────────────────────────

    void leftRotate(Node* x) {
        Node* y   = x->right;
        x->right  = y->left;
        if (y->left != NIL) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == NIL)          root           = y;
        else if (x == x->parent->left) x->parent->left  = y;
        else                            x->parent->right = y;
        y->left   = x;
        x->parent = y;
        cout << "[LEFT ROTATE]  at " << x->key << "\n";
    }

    void rightRotate(Node* x) {
        Node* y   = x->left;
        x->left   = y->right;
        if (y->right != NIL) y->right->parent = x;
        y->parent = x->parent;
        if (x->parent == NIL)           root            = y;
        else if (x == x->parent->right) x->parent->right = y;
        else                             x->parent->left  = y;
        y->right  = x;
        x->parent = y;
        cout << "[RIGHT ROTATE] at " << x->key << "\n";
    }

    // ── Fix-up after insertion ─────────────────────────────────────

    void fixInsert(Node* z) {
        while (z->parent->color == RED) {
            if (z->parent == z->parent->parent->left) {
                Node* uncle = z->parent->parent->right;
                if (uncle->color == RED) {
                    // Case 1: uncle is RED – recolor
                    z->parent->color          = BLACK;
                    uncle->color              = BLACK;
                    z->parent->parent->color  = RED;
                    cout << "[RECOLOR] around " << z->parent->parent->key << "\n";
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        // Case 2: triangle – left rotate parent
                        z = z->parent;
                        leftRotate(z);
                    }
                    // Case 3: line – right rotate grandparent
                    z->parent->color         = BLACK;
                    z->parent->parent->color = RED;
                    rightRotate(z->parent->parent);
                }
            } else {
                Node* uncle = z->parent->parent->left;
                if (uncle->color == RED) {
                    z->parent->color          = BLACK;
                    uncle->color              = BLACK;
                    z->parent->parent->color  = RED;
                    cout << "[RECOLOR] around " << z->parent->parent->key << "\n";
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rightRotate(z);
                    }
                    z->parent->color         = BLACK;
                    z->parent->parent->color = RED;
                    leftRotate(z->parent->parent);
                }
            }
        }
        root->color = BLACK;
    }

    // ── Helpers ───────────────────────────────────────────────────

    Node* searchHelper(Node* node, int key) const {
        if (node == NIL || node->key == key) return node;
        return (key < node->key) ? searchHelper(node->left, key)
                                 : searchHelper(node->right, key);
    }

    void inorderHelper(Node* node) const {
        if (node == NIL) return;
        inorderHelper(node->left);
        cout << node->key << "(" << colorStr(node->color)[0] << ") ";
        inorderHelper(node->right);
    }

    // Returns true if subtree is valid; sets bh = black-height of subtree.
    bool verifyHelper(Node* node, int& bh) const {
        if (node == NIL) { bh = 1; return true; }
        // No two consecutive RED nodes
        if (node->color == RED &&
            (node->left->color == RED || node->right->color == RED))
            return false;
        int lBH = 0, rBH = 0;
        if (!verifyHelper(node->left, lBH) || !verifyHelper(node->right, rBH))
            return false;
        if (lBH != rBH) return false;
        bh = lBH + (node->color == BLACK ? 1 : 0);
        return true;
    }
};

int main() {
    cout << "=== Lab 5: Red-Black Tree Implementation ===\n\n";

    RedBlackTree rbt;

    // Task 2: Node Insertion
    cout << "--- Task 2: Node Insertion ---\n";
    for (int v : {10, 20, 30, 15, 25, 5, 1})
        rbt.insert(v);
    cout << "\n";

    // Task 5: Inorder Traversal
    cout << "--- Task 5: Inorder Traversal ---\n";
    rbt.inorder();
    cout << "\n";

    // Task 4: Search Operations
    cout << "--- Task 4: Search Operations ---\n";
    rbt.search(15);
    rbt.search(25);
    rbt.search(99);
    cout << "\n";

    // Task 6: Property Verification
    cout << "--- Task 6: Property Verification ---\n";
    rbt.verifyProperties();

    return 0;
}
