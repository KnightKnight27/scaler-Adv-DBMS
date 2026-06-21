// Lab 5 - Red-Black Tree
// Akshat Kushwaha | 24BCS10060
//
// A red-black tree (a self-balancing binary search tree) with insert, search,
// in-order print and a validity checker. I used the "sentinel" style from CLRS:
// instead of nullptr, every leaf/parent-of-root points to one shared black
// node called NIL. That removes a lot of null checks inside the fixup code.
//
// The four rules a red-black tree keeps:
//   1. every node is red or black
//   2. the root is black
//   3. a red node cannot have a red child
//   4. every path from a node down to a NIL passes the same number of blacks
// These together keep the height about 2*log2(n), so search/insert are O(log n).
//
// Build: g++ -std=c++17 -Wall -Wextra rb_tree.cpp -o rb_tree
// Run:   ./rb_tree

#include <iostream>
#include <vector>

enum Color { RED, BLACK };

struct Node {
    int    key;
    Color  color;
    Node*  left;
    Node*  right;
    Node*  parent;
};

class RBTree {
public:
    RBTree() {
        nil_ = new Node{0, BLACK, nullptr, nullptr, nullptr};
        root_ = nil_;
    }

    void insert(int key) {
        Node* node = new Node{key, RED, nil_, nil_, nil_};
        // ordinary BST placement
        Node* parent = nil_;
        Node* cur = root_;
        while (cur != nil_) {
            parent = cur;
            cur = (key < cur->key) ? cur->left : cur->right;
        }
        node->parent = parent;
        if (parent == nil_)            root_ = node;
        else if (key < parent->key)    parent->left = node;
        else                           parent->right = node;
        fix_after_insert(node);
    }

    bool contains(int key) const {
        Node* cur = root_;
        while (cur != nil_) {
            if (key == cur->key) return true;
            cur = (key < cur->key) ? cur->left : cur->right;
        }
        return false;
    }

    // In-order walk: prints keys in sorted order, with each node's color.
    void print_sorted() const {
        inorder(root_);
        std::cout << "\n";
    }

    // Returns true if all four red-black rules hold. Used as a self-check.
    bool valid() const {
        if (root_->color != BLACK) return false;       // rule 2
        int dummy = 0;
        return check(root_, dummy);
    }

private:
    Node* root_;
    Node* nil_;     // the single shared sentinel leaf (always black)

    void rotate_left(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != nil_) y->left->parent = x;
        y->parent = x->parent;
        if      (x->parent == nil_)      root_ = y;
        else if (x == x->parent->left)   x->parent->left = y;
        else                             x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void rotate_right(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right != nil_) y->right->parent = x;
        y->parent = x->parent;
        if      (x->parent == nil_)      root_ = y;
        else if (x == x->parent->right)  x->parent->right = y;
        else                             x->parent->left = y;
        y->right = x;
        x->parent = y;
    }

    // Restore the rules after inserting a red node.
    void fix_after_insert(Node* z) {
        while (z->parent->color == RED) {
            Node* gp = z->parent->parent;
            if (z->parent == gp->left) {
                Node* uncle = gp->right;
                if (uncle->color == RED) {              // case 1: just recolor
                    z->parent->color = BLACK;
                    uncle->color     = BLACK;
                    gp->color        = RED;
                    z = gp;
                } else {
                    if (z == z->parent->right) {        // case 2: make it a line
                        z = z->parent;
                        rotate_left(z);
                    }
                    z->parent->color = BLACK;           // case 3: recolor + rotate
                    gp->color        = RED;
                    rotate_right(gp);
                }
            } else {                                     // mirror image
                Node* uncle = gp->left;
                if (uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color     = BLACK;
                    gp->color        = RED;
                    z = gp;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rotate_right(z);
                    }
                    z->parent->color = BLACK;
                    gp->color        = RED;
                    rotate_left(gp);
                }
            }
        }
        root_->color = BLACK;       // rule 2, always
    }

    void inorder(Node* n) const {
        if (n == nil_) return;
        inorder(n->left);
        std::cout << n->key << (n->color == RED ? "(R) " : "(B) ");
        inorder(n->right);
    }

    // Recursive check: returns the black-height, fills `ok` via return value
    // by returning false up the chain if any rule is broken.
    bool check(Node* n, int& black_height) const {
        if (n == nil_) { black_height = 1; return true; }
        // rule 3: red node cannot have a red child
        if (n->color == RED) {
            if (n->left->color == RED || n->right->color == RED) return false;
        }
        int lh = 0, rh = 0;
        if (!check(n->left, lh))  return false;
        if (!check(n->right, rh)) return false;
        if (lh != rh) return false;                      // rule 4
        black_height = lh + (n->color == BLACK ? 1 : 0);
        return true;
    }
};

int main() {
    RBTree tree;
    const std::vector<int> keys = {50, 30, 70, 20, 40, 60, 80, 10, 25, 35};

    std::cout << "Red-Black Tree | Akshat Kushwaha | 24BCS10060\n";
    std::cout << "inserting: ";
    for (int k : keys) { std::cout << k << " "; tree.insert(k); }
    std::cout << "\n\n";

    std::cout << "in-order (should be sorted, R=red B=black):\n";
    tree.print_sorted();

    std::cout << "\nsearches:\n";
    for (int q : {40, 99, 10, 55}) {
        std::cout << "  contains(" << q << ") = "
                  << (tree.contains(q) ? "yes" : "no") << "\n";
    }

    std::cout << "\nall red-black rules hold? "
              << (tree.valid() ? "yes" : "NO") << "\n";
    return 0;
}
