// Lab 5 — Red-Black Tree (insert + search + inorder traversal)
// Author: 24BCS10183 Aman Yadav  (Class B, 2nd year)
//
// Build: g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 RB_tree.cpp -o rb_tree
// Run:   ./rb_tree
//
// Implements a CLRS-style red-black BST:
//   - insert(v) does standard BST insert, paints the new node red, then
//     runs insertFixup to restore the four RB invariants.
//   - find(v) is plain BST search.
//   - inorder() yields a sorted vector (proof that the BST order holds).
//   - printPretty() draws the tree sideways for visual inspection.
//   - validate() asserts: root is black, no red node has a red child,
//     every root-to-NIL path has the same black-height, and the BST
//     order holds globally.

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

enum class Color { Red, Black };

struct Node {
    int   val;
    Color color;
    Node *left, *right, *parent;

    explicit Node(int v, Color c = Color::Red)
        : val(v), color(c), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
public:
    RedBlackTree() : root_(nullptr) {}

    ~RedBlackTree() { destroy(root_); }

    RedBlackTree(const RedBlackTree&) = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    void insert(int value) {
        Node* z = new Node(value, Color::Red);
        bstInsert(z);
        insertFixup(z);
    }

    // Returns the node with val == value, or nullptr on miss.
    const Node* find(int value) const {
        Node* cur = root_;
        while (cur) {
            if (value == cur->val) return cur;
            cur = (value < cur->val) ? cur->left : cur->right;
        }
        return nullptr;
    }

    std::vector<int> inorder() const {
        std::vector<int> out;
        inorderImpl(root_, out);
        return out;
    }

    // Prints the tree sideways; the rightmost subtree appears at the top
    // so the picture matches how we draw trees on paper (root on the left).
    void printPretty(std::ostream& os) const { printPretty(os, root_, 0); }

    // Validates the four RB invariants:
    //   1. root is black
    //   2. no red node has a red child
    //   3. every root-to-NIL path has the same number of black nodes
    //   4. inorder traversal is strictly sorted (BST order)
    bool validate() const {
        if (!root_) return true;
        if (root_->color != Color::Black) return false;
        if (blackHeight(root_) == -1) return false;
        return isBst(root_, nullptr, nullptr);
    }

private:
    Node* root_;

    void destroy(Node* n) {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }

    void leftRotate(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if      (!x->parent)            root_           = y;
        else if (x == x->parent->left)  x->parent->left  = y;
        else                            x->parent->right = y;
        y->left   = x;
        x->parent = y;
    }

    void rightRotate(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        y->parent = x->parent;
        if      (!x->parent)             root_            = y;
        else if (x == x->parent->left)   x->parent->left  = y;
        else                             x->parent->right = y;
        y->right  = x;
        x->parent = y;
    }

    void bstInsert(Node* z) {
        Node* parent = nullptr;
        Node* cur    = root_;
        while (cur) {
            parent = cur;
            cur = (z->val < cur->val) ? cur->left : cur->right;
        }
        z->parent = parent;
        if      (!parent)             root_         = z;
        else if (z->val < parent->val) parent->left  = z;
        else                           parent->right = z;
    }

    void insertFixup(Node* z) {
        while (z->parent && z->parent->color == Color::Red) {
            Node* parent      = z->parent;
            Node* grandparent = parent->parent;
            if (parent == grandparent->left) {
                Node* uncle = grandparent->right;
                if (uncle && uncle->color == Color::Red) {
                    // Case 1: uncle red → recolor and walk up.
                    parent->color      = Color::Black;
                    uncle->color       = Color::Black;
                    grandparent->color = Color::Red;
                    z = grandparent;
                } else {
                    if (z == parent->right) {
                        // Case 2: zig-zag → rotate parent into a straight line.
                        z = parent;
                        leftRotate(z);
                        parent = z->parent;
                    }
                    // Case 3: straight line → recolor and rotate grandparent.
                    parent->color      = Color::Black;
                    grandparent->color = Color::Red;
                    rightRotate(grandparent);
                }
            } else {
                Node* uncle = grandparent->left;
                if (uncle && uncle->color == Color::Red) {
                    parent->color      = Color::Black;
                    uncle->color       = Color::Black;
                    grandparent->color = Color::Red;
                    z = grandparent;
                } else {
                    if (z == parent->left) {
                        z = parent;
                        rightRotate(z);
                        parent = z->parent;
                    }
                    parent->color      = Color::Black;
                    grandparent->color = Color::Red;
                    leftRotate(grandparent);
                }
            }
        }
        root_->color = Color::Black;
    }

    static void inorderImpl(const Node* n, std::vector<int>& out) {
        if (!n) return;
        inorderImpl(n->left, out);
        out.push_back(n->val);
        inorderImpl(n->right, out);
    }

    static void printPretty(std::ostream& os, const Node* n, int depth) {
        if (!n) return;
        printPretty(os, n->right, depth + 1);
        for (int i = 0; i < depth; ++i) os << "    ";
        os << n->val << (n->color == Color::Red ? "(R)" : "(B)") << '\n';
        printPretty(os, n->left, depth + 1);
    }

    // Returns the uniform black-height (NIL counts as black) or -1 if the
    // subtree breaks the no-red-red or equal-black-height rule.
    static int blackHeight(const Node* n) {
        if (!n) return 1;
        if (n->color == Color::Red) {
            if ((n->left  && n->left->color  == Color::Red) ||
                (n->right && n->right->color == Color::Red)) return -1;
        }
        int lh = blackHeight(n->left);
        int rh = blackHeight(n->right);
        if (lh == -1 || rh == -1 || lh != rh) return -1;
        return lh + (n->color == Color::Black ? 1 : 0);
    }

    static bool isBst(const Node* n, const int* lo, const int* hi) {
        if (!n) return true;
        if (lo && n->val <= *lo) return false;
        if (hi && n->val >= *hi) return false;
        return isBst(n->left, lo, &n->val) && isBst(n->right, &n->val, hi);
    }
};

int main() {
    RedBlackTree tree;
    const std::vector<int> input = {10, 20, 30, 15, 25, 5, 1, 8, 40, 35};

    std::cout << "Inserting: ";
    for (int v : input) {
        std::cout << v << ' ';
        tree.insert(v);
    }
    std::cout << "\n\nTree (right-rotated 90 deg; R=red, B=black):\n";
    tree.printPretty(std::cout);

    std::cout << "\nInorder (must be sorted): ";
    auto sorted = tree.inorder();
    for (int v : sorted) std::cout << v << ' ';
    std::cout << '\n';

    std::cout << "\nLookups:\n";
    for (int q : {15, 99, 1, 41}) {
        std::cout << "  find(" << q << ") -> "
                  << (tree.find(q) ? "found" : "miss") << '\n';
    }

    const bool ok = tree.validate();
    std::cout << "\nRB invariants hold: " << (ok ? "yes" : "NO") << '\n';
    assert(ok);

    // Sanity: inorder must equal the sorted input.
    std::vector<int> expected = input;
    std::sort(expected.begin(), expected.end());
    assert(sorted == expected);

    return 0;
}
