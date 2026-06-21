// Lab 5 - Red-Black Tree (insert, search, delete)
// Bibek Jyoti Charah (24bcs10112)
//
// Sentinel-based RBT: every empty child / the root's parent points at a
// single shared black `nil` node, which removes most of the null checks the
// delete fixup would otherwise need.

#include <iostream>
#include <vector>

class RedBlackTree {
    enum Color { RED, BLACK };

    struct Node {
        int key;
        Color color;
        Node *left, *right, *parent;
        Node(int k, Color c, Node *nil) : key(k), color(c), left(nil), right(nil), parent(nil) {}
    };

    Node *nil;   // shared sentinel
    Node *root;

    void rotateLeft(Node *x) {
        Node *y = x->right;
        x->right = y->left;
        if (y->left != nil) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == nil)            root = y;
        else if (x == x->parent->left)   x->parent->left = y;
        else                             x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void rotateRight(Node *x) {
        Node *y = x->left;
        x->left = y->right;
        if (y->right != nil) y->right->parent = x;
        y->parent = x->parent;
        if (x->parent == nil)            root = y;
        else if (x == x->parent->right)  x->parent->right = y;
        else                             x->parent->left = y;
        y->right = x;
        x->parent = y;
    }

    void insertFixup(Node *z) {
        while (z->parent->color == RED) {
            Node *gp = z->parent->parent;
            if (z->parent == gp->left) {
                Node *uncle = gp->right;
                if (uncle->color == RED) {                 // recolour and climb
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;
                    z = gp;
                } else {
                    if (z == z->parent->right) {           // bend into a line
                        z = z->parent;
                        rotateLeft(z);
                    }
                    z->parent->color = BLACK;
                    z->parent->parent->color = RED;
                    rotateRight(z->parent->parent);
                }
            } else {                                       // mirror image
                Node *uncle = gp->left;
                if (uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;
                    z = gp;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rotateRight(z);
                    }
                    z->parent->color = BLACK;
                    z->parent->parent->color = RED;
                    rotateLeft(z->parent->parent);
                }
            }
        }
        root->color = BLACK;
    }

    void transplant(Node *u, Node *v) {
        if (u->parent == nil)           root = v;
        else if (u == u->parent->left)  u->parent->left = v;
        else                            u->parent->right = v;
        v->parent = u->parent;
    }

    Node *minimum(Node *x) const {
        while (x->left != nil) x = x->left;
        return x;
    }

    Node *find(int key) const {
        Node *x = root;
        while (x != nil && x->key != key)
            x = key < x->key ? x->left : x->right;
        return x;
    }

    void eraseFixup(Node *x) {
        while (x != root && x->color == BLACK) {
            if (x == x->parent->left) {
                Node *w = x->parent->right;            // sibling
                if (w->color == RED) {
                    w->color = BLACK;
                    x->parent->color = RED;
                    rotateLeft(x->parent);
                    w = x->parent->right;
                }
                if (w->left->color == BLACK && w->right->color == BLACK) {
                    w->color = RED;
                    x = x->parent;
                } else {
                    if (w->right->color == BLACK) {
                        w->left->color = BLACK;
                        w->color = RED;
                        rotateRight(w);
                        w = x->parent->right;
                    }
                    w->color = x->parent->color;
                    x->parent->color = BLACK;
                    w->right->color = BLACK;
                    rotateLeft(x->parent);
                    x = root;
                }
            } else {                                   // mirror image
                Node *w = x->parent->left;
                if (w->color == RED) {
                    w->color = BLACK;
                    x->parent->color = RED;
                    rotateRight(x->parent);
                    w = x->parent->left;
                }
                if (w->right->color == BLACK && w->left->color == BLACK) {
                    w->color = RED;
                    x = x->parent;
                } else {
                    if (w->left->color == BLACK) {
                        w->right->color = BLACK;
                        w->color = RED;
                        rotateLeft(w);
                        w = x->parent->left;
                    }
                    w->color = x->parent->color;
                    x->parent->color = BLACK;
                    w->left->color = BLACK;
                    rotateRight(x->parent);
                    x = root;
                }
            }
        }
        x->color = BLACK;
    }

    void inorder(Node *x) const {
        if (x == nil) return;
        inorder(x->left);
        std::cout << x->key << (x->color == RED ? "(R) " : "(B) ");
        inorder(x->right);
    }

    // Black-height of the subtree, or -1 if any red-black rule is broken.
    int blackHeight(Node *x) const {
        if (x == nil) return 1;
        if (x->color == RED && (x->left->color == RED || x->right->color == RED))
            return -1;
        int lh = blackHeight(x->left);
        int rh = blackHeight(x->right);
        if (lh == -1 || rh == -1 || lh != rh) return -1;
        return lh + (x->color == BLACK ? 1 : 0);
    }

    void destroy(Node *x) {
        if (x == nil) return;
        destroy(x->left);
        destroy(x->right);
        delete x;
    }

public:
    RedBlackTree() {
        nil = new Node(0, BLACK, nullptr);
        nil->left = nil->right = nil->parent = nil;
        root = nil;
    }
    ~RedBlackTree() { destroy(root); delete nil; }

    RedBlackTree(const RedBlackTree &) = delete;
    RedBlackTree &operator=(const RedBlackTree &) = delete;

    void insert(int key) {
        Node *z = new Node(key, RED, nil);
        Node *parent = nil, *x = root;
        while (x != nil) {
            parent = x;
            x = key < x->key ? x->left : x->right;
        }
        z->parent = parent;
        if (parent == nil)          root = z;
        else if (key < parent->key) parent->left = z;
        else                        parent->right = z;
        insertFixup(z);
    }

    bool contains(int key) const { return find(key) != nil; }

    bool erase(int key) {
        Node *z = find(key);
        if (z == nil) return false;

        Node *y = z;
        Color yColor = y->color;
        Node *x;
        if (z->left == nil) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == nil) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);             // in-order successor
            yColor = y->color;
            x = y->right;
            if (y->parent == z) {
                x->parent = y;                 // keeps fixup correct when x == nil
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
        if (yColor == BLACK) eraseFixup(x);
        return true;
    }

    void print() const { inorder(root); std::cout << "\n"; }
    bool valid() const { return root->color == BLACK && blackHeight(root) != -1; }
};

int main() {
    RedBlackTree tree;

    std::vector<int> keys = {10, 20, 30, 15, 25, 5, 1, 35, 40, 50, 45};
    for (int k : keys) tree.insert(k);

    std::cout << "Inorder after inserts:\n  ";
    tree.print();
    std::cout << "Valid RB tree: " << (tree.valid() ? "yes" : "no") << "\n\n";

    std::cout << "contains(25) = " << (tree.contains(25) ? "yes" : "no") << "\n";
    std::cout << "contains(99) = " << (tree.contains(99) ? "yes" : "no") << "\n\n";

    for (int k : {20, 1, 30, 50}) {
        tree.erase(k);
        std::cout << "After erase(" << k << "):\n  ";
        tree.print();
        std::cout << "  valid: " << (tree.valid() ? "yes" : "no") << "\n";
    }
    return 0;
}
