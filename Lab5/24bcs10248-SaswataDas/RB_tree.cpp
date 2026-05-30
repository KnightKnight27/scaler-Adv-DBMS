// Lab 5 — Red-Black Tree (insert + search + inorder traversal)
// Author: 24BCS10248 Saswata Das
//
// Build: g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 RB_tree.cpp -o rb_tree
// Run:   ./rb_tree
//
// Implements a CLRS-style red-black BST:
//   - insert(v) does standard BST insert, paints the new node red, then
//     runs fixup to restore the four RB invariants.
//   - search(v) is plain BST search.
//   - inorder() yields a sorted vector (proof that the BST order holds).
//   - display() draws the tree sideways for visual inspection.
//   - checkInvariants() asserts: root is black, no red-red parent-child,
//     every root-to-NIL path has the same black-height, and BST order holds.

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

enum class Color { Red, Black };

struct RBNode {
    int   key;
    Color color;
    RBNode *left, *right, *parent;

    explicit RBNode(int k, Color c = Color::Red)
        : key(k), color(c), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RBTree {
public:
    RBTree() : root_(nullptr) {}

    ~RBTree() { freeSubtree(root_); }

    RBTree(const RBTree&) = delete;
    RBTree& operator=(const RBTree&) = delete;

    void insert(int key) {
        RBNode* node = new RBNode(key, Color::Red);
        bstPlace(node);
        repairAfterInsert(node);
    }

    const RBNode* search(int key) const {
        RBNode* cur = root_;
        while (cur) {
            if (key == cur->key) return cur;
            cur = (key < cur->key) ? cur->left : cur->right;
        }
        return nullptr;
    }

    std::vector<int> inorder() const {
        std::vector<int> result;
        collectInorder(root_, result);
        return result;
    }

    void display(std::ostream& os) const { printSideways(os, root_, 0); }

    bool checkInvariants() const {
        if (!root_) return true;
        if (root_->color != Color::Black) return false;
        if (computeBlackHeight(root_) == -1) return false;
        return verifyBstOrder(root_, nullptr, nullptr);
    }

private:
    RBNode* root_;

    void freeSubtree(RBNode* n) {
        if (!n) return;
        freeSubtree(n->left);
        freeSubtree(n->right);
        delete n;
    }

    void rotateLeft(RBNode* x) {
        RBNode* y = x->right;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if      (!x->parent)            root_           = y;
        else if (x == x->parent->left)  x->parent->left  = y;
        else                            x->parent->right = y;
        y->left   = x;
        x->parent = y;
    }

    void rotateRight(RBNode* x) {
        RBNode* y = x->left;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        y->parent = x->parent;
        if      (!x->parent)             root_            = y;
        else if (x == x->parent->left)   x->parent->left  = y;
        else                             x->parent->right = y;
        y->right  = x;
        x->parent = y;
    }

    void bstPlace(RBNode* z) {
        RBNode* par = nullptr;
        RBNode* cur = root_;
        while (cur) {
            par = cur;
            cur = (z->key < cur->key) ? cur->left : cur->right;
        }
        z->parent = par;
        if      (!par)               root_       = z;
        else if (z->key < par->key)  par->left   = z;
        else                         par->right  = z;
    }

    void repairAfterInsert(RBNode* z) {
        while (z->parent && z->parent->color == Color::Red) {
            RBNode* par = z->parent;
            RBNode* gp  = par->parent;
            if (par == gp->left) {
                RBNode* uncle = gp->right;
                if (uncle && uncle->color == Color::Red) {
                    par->color   = Color::Black;
                    uncle->color = Color::Black;
                    gp->color    = Color::Red;
                    z = gp;
                } else {
                    if (z == par->right) {
                        z = par;
                        rotateLeft(z);
                        par = z->parent;
                    }
                    par->color = Color::Black;
                    gp->color  = Color::Red;
                    rotateRight(gp);
                }
            } else {
                RBNode* uncle = gp->left;
                if (uncle && uncle->color == Color::Red) {
                    par->color   = Color::Black;
                    uncle->color = Color::Black;
                    gp->color    = Color::Red;
                    z = gp;
                } else {
                    if (z == par->left) {
                        z = par;
                        rotateRight(z);
                        par = z->parent;
                    }
                    par->color = Color::Black;
                    gp->color  = Color::Red;
                    rotateLeft(gp);
                }
            }
        }
        root_->color = Color::Black;
    }

    static void collectInorder(const RBNode* n, std::vector<int>& out) {
        if (!n) return;
        collectInorder(n->left, out);
        out.push_back(n->key);
        collectInorder(n->right, out);
    }

    static void printSideways(std::ostream& os, const RBNode* n, int depth) {
        if (!n) return;
        printSideways(os, n->right, depth + 1);
        for (int i = 0; i < depth; ++i) os << "    ";
        os << n->key << (n->color == Color::Red ? "(R)" : "(B)") << '\n';
        printSideways(os, n->left, depth + 1);
    }

    static int computeBlackHeight(const RBNode* n) {
        if (!n) return 1;
        if (n->color == Color::Red) {
            if ((n->left  && n->left->color  == Color::Red) ||
                (n->right && n->right->color == Color::Red)) return -1;
        }
        int lh = computeBlackHeight(n->left);
        int rh = computeBlackHeight(n->right);
        if (lh == -1 || rh == -1 || lh != rh) return -1;
        return lh + (n->color == Color::Black ? 1 : 0);
    }

    static bool verifyBstOrder(const RBNode* n, const int* lo, const int* hi) {
        if (!n) return true;
        if (lo && n->key <= *lo) return false;
        if (hi && n->key >= *hi) return false;
        return verifyBstOrder(n->left, lo, &n->key) &&
               verifyBstOrder(n->right, &n->key, hi);
    }
};

int main() {
    RBTree tree;
    const std::vector<int> keys = {12, 24, 36, 18, 30, 6, 3, 9, 48, 42};

    std::cout << "Inserting: ";
    for (int k : keys) {
        std::cout << k << ' ';
        tree.insert(k);
    }
    std::cout << "\n\nTree (right-rotated 90 deg; R=red, B=black):\n";
    tree.display(std::cout);

    std::cout << "\nInorder (must be sorted): ";
    auto sorted = tree.inorder();
    for (int v : sorted) std::cout << v << ' ';
    std::cout << '\n';

    std::cout << "\nSearches:\n";
    for (int q : {18, 99, 3, 50}) {
        std::cout << "  search(" << q << ") -> "
                  << (tree.search(q) ? "found" : "miss") << '\n';
    }

    const bool ok = tree.checkInvariants();
    std::cout << "\nRB invariants hold: " << (ok ? "yes" : "NO") << '\n';
    assert(ok);

    std::vector<int> expected = keys;
    std::sort(expected.begin(), expected.end());
    assert(sorted == expected);

    return 0;
}
