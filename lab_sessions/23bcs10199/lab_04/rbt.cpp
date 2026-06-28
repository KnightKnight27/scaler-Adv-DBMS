// Lab Session 4 — Red-Black Tree
// Student : Indrajeet Yadav | Roll No: 23BCS10199
//
// Implements a full Red-Black Tree with:
//   insert  — BST insert + rebalancing via rotations and recoloring
//   remove  — 3-case delete + fix_delete for double-black resolution
//   search  — O(log n) lookup
//   inorder — sorted traversal (verifies BST property)
//   height  — verifies O(log n) bound
//   verify  — runtime checks of all 4 RB properties
//
// Red-Black Tree guarantees O(log n) for insert, search, and delete because
// the 4 invariants bound the tree height to at most 2*log2(n+1).
//
// Build: g++ -std=c++17 -Wall -Wextra -O2 rbt.cpp -o rbt
// Run:   ./rbt

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <algorithm>
#include <iomanip>
#include <queue>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Node color and sentinel nil node
// ─────────────────────────────────────────────────────────────────────────────

enum Color { RED, BLACK };

struct RBNode {
    int     key;
    Color   color;
    RBNode* left;
    RBNode* right;
    RBNode* parent;

    explicit RBNode(int k, Color c = RED, RBNode* nil = nullptr)
        : key(k), color(c), left(nil), right(nil), parent(nil) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// RedBlackTree
// ─────────────────────────────────────────────────────────────────────────────

class RedBlackTree {
public:
    RedBlackTree() {
        // Sentinel nil node: BLACK, all pointers point to itself
        nil_ = new RBNode(0, BLACK, nullptr);
        nil_->left = nil_->right = nil_->parent = nil_;
        root_ = nil_;
    }

    ~RedBlackTree() { destroy(root_); delete nil_; }

    // ── insert ────────────────────────────────────────────────────────────────
    void insert(int key) {
        RBNode* z = new RBNode(key, RED, nil_);
        z->left = z->right = z->parent = nil_;

        // Standard BST insert
        RBNode* y = nil_;
        RBNode* x = root_;
        while (x != nil_) {
            y = x;
            if (z->key < x->key)      x = x->left;
            else if (z->key > x->key) x = x->right;
            else { delete z; return; } // duplicate keys ignored
        }
        z->parent = y;
        if (y == nil_)               root_ = z;
        else if (z->key < y->key)    y->left = z;
        else                          y->right = z;

        // Fix RB properties after BST insert
        fix_insert(z);
        size_++;
    }

    // ── remove ───────────────────────────────────────────────────────────────
    void remove(int key) {
        RBNode* z = search_node(root_, key);
        if (z == nil_) return;
        delete_node(z);
        size_--;
    }

    // ── search ───────────────────────────────────────────────────────────────
    bool search(int key) const {
        return search_node(root_, key) != nil_;
    }

    // ── inorder traversal ────────────────────────────────────────────────────
    std::vector<int> inorder() const {
        std::vector<int> result;
        inorder_helper(root_, result);
        return result;
    }

    // ── tree height ──────────────────────────────────────────────────────────
    int height() const { return height_helper(root_); }

    // ── black height (must be same for all paths) ─────────────────────────────
    int black_height() const { return black_height_helper(root_); }

    // ── size ─────────────────────────────────────────────────────────────────
    int size() const { return size_; }

    // ── verify all 4 Red-Black properties ─────────────────────────────────────
    bool verify() const {
        if (root_ == nil_) return true;
        // Property 2: root must be black
        if (root_->color != BLACK) {
            std::cerr << "[FAIL] Property 2: root is not BLACK\n";
            return false;
        }
        // Properties 1,3,4 verified recursively
        int bh = -1;
        return verify_helper(root_, 0, bh);
    }

    // ── print level-order (BFS) with colors ──────────────────────────────────
    void print_tree() const {
        if (root_ == nil_) { std::cout << "(empty)\n"; return; }
        std::queue<RBNode*> q;
        q.push(root_);
        int level = 0;
        while (!q.empty()) {
            int n = q.size();
            std::cout << "  L" << level << ": ";
            while (n--) {
                RBNode* node = q.front(); q.pop();
                if (node == nil_) { std::cout << "__  "; continue; }
                std::cout << node->key
                          << (node->color == RED ? "R" : "B") << "  ";
                if (node->left != nil_ || node->right != nil_) {
                    q.push(node->left);
                    q.push(node->right);
                }
            }
            std::cout << "\n";
            level++;
        }
    }

private:
    RBNode* root_;
    RBNode* nil_;   // sentinel — all null pointers point here
    int     size_ = 0;

    // ── rotations ─────────────────────────────────────────────────────────────
    //
    // Left rotation on x:
    //
    //     x                y
    //    (L)(R)   ->      (L)(R)
    //   a    y           x     g
    //       (L)(R)      (L)(R)
    //      b    g      a    b
    //
    // Preserves BST order: a < x < b < y < g
    void left_rotate(RBNode* x) {
        RBNode* y = x->right;          // y is x's right child
        x->right = y->left;            // β becomes x's right child
        if (y->left != nil_) y->left->parent = x;

        y->parent = x->parent;         // link y to x's parent
        if (x->parent == nil_)         root_ = y;
        else if (x == x->parent->left) x->parent->left  = y;
        else                           x->parent->right = y;

        y->left   = x;                 // x becomes y's left child
        x->parent = y;
    }

    // Right rotation on x (mirror of left_rotate):
    //
    //       x              y
    //      (L)(R)  ->     (L)(R)
    //     y    g         a    x
    //    (L)(R)              (L)(R)
    //   a    b              b    g
    void right_rotate(RBNode* x) {
        RBNode* y = x->left;
        x->left = y->right;
        if (y->right != nil_) y->right->parent = x;

        y->parent = x->parent;
        if (x->parent == nil_)          root_ = y;
        else if (x == x->parent->right) x->parent->right = y;
        else                            x->parent->left  = y;

        y->right  = x;
        x->parent = y;
    }

    // ── fix_insert: restore RB properties after BST insert ───────────────────
    //
    // The newly inserted node z is RED. If its parent is also RED, Property 3
    // (no two consecutive RED nodes) is violated. There are 3 cases:
    //
    // Case 1: Uncle is RED
    //   → Recolor: parent and uncle → BLACK, grandparent → RED
    //   → Move z up to grandparent and repeat
    //
    // Case 2: Uncle is BLACK, z is a "triangle" (parent-z forms an angle)
    //   → Rotate z's parent in the opposite direction to make it a "line"
    //   → Fall through to Case 3
    //
    // Case 3: Uncle is BLACK, z is a "line" (parent-z are in the same direction)
    //   → Recolor parent → BLACK, grandparent → RED
    //   → Rotate grandparent in the opposite direction
    //   → Done (no more violations upward)
    void fix_insert(RBNode* z) {
        while (z->parent->color == RED) {
            RBNode* gp = z->parent->parent;  // grandparent (exists because parent is RED, not root)

            if (z->parent == gp->left) {
                RBNode* uncle = gp->right;

                if (uncle->color == RED) {
                    // Case 1: uncle is RED — recolor and move up
                    z->parent->color = BLACK;
                    uncle->color     = BLACK;
                    gp->color        = RED;
                    z = gp;            // next violation might be at grandparent
                } else {
                    if (z == z->parent->right) {
                        // Case 2: "triangle" → rotate to become a "line"
                        z = z->parent;
                        left_rotate(z);
                    }
                    // Case 3: "line" → recolor + rotate grandparent
                    z->parent->color = BLACK;
                    gp->color        = RED;
                    right_rotate(gp);
                }
            } else {
                // Mirror: z->parent is gp->right
                RBNode* uncle = gp->left;

                if (uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color     = BLACK;
                    gp->color        = RED;
                    z = gp;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        right_rotate(z);
                    }
                    z->parent->color = BLACK;
                    gp->color        = RED;
                    left_rotate(gp);
                }
            }
        }
        root_->color = BLACK;  // Property 2: root must always be BLACK
    }

    // ── transplant: replace subtree rooted at u with subtree rooted at v ──────
    void transplant(RBNode* u, RBNode* v) {
        if (u->parent == nil_)          root_ = v;
        else if (u == u->parent->left)  u->parent->left  = v;
        else                            u->parent->right = v;
        v->parent = u->parent;          // even if v == nil_, this is correct
    }

    // ── delete_node: remove node z ────────────────────────────────────────────
    //
    // Three structural cases for deletion:
    // Case A: z has no left child → replace z with its right child
    // Case B: z has no right child → replace z with its left child
    // Case C: z has two children → find in-order successor y (leftmost of
    //         z's right subtree), copy y's key into z, delete y instead.
    //         y has at most one child (its right child), so deleting y is
    //         either Case A or B.
    //
    // After structural removal, if the removed node (or y) was BLACK, we
    // may have a "double-black" deficit in the black-height. fix_delete()
    // resolves it.
    void delete_node(RBNode* z) {
        RBNode* y = z;                    // y is the node actually removed from the tree
        RBNode* x = nil_;                 // x replaces y
        Color   y_orig_color = y->color;

        if (z->left == nil_) {
            // Case A: z has no left child
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == nil_) {
            // Case B: z has no right child
            x = z->left;
            transplant(z, z->left);
        } else {
            // Case C: z has two children — find successor (minimum of right subtree)
            y = minimum(z->right);
            y_orig_color = y->color;
            x = y->right;

            if (y->parent == z) {
                // y is the direct right child of z
                x->parent = y;
            } else {
                // y is deeper in the right subtree
                transplant(y, y->right);
                y->right         = z->right;
                y->right->parent = y;
            }
            transplant(z, y);
            y->left         = z->left;
            y->left->parent = y;
            y->color        = z->color;  // y inherits z's color
        }

        delete z;

        // Fix double-black if a BLACK node was removed
        if (y_orig_color == BLACK)
            fix_delete(x);
    }

    // ── fix_delete: resolve double-black at node x ────────────────────────────
    //
    // A "double-black" node x means: the path through x has one fewer BLACK
    // node than all other paths (Property 4 violated). We fix it by pushing
    // the extra blackness up the tree via rotations and recoloring.
    //
    // 4 cases (plus mirror cases):
    // Case 1: Sibling w is RED
    //   → Recolor w BLACK, parent RED; rotate parent toward x
    //   → Converts to Case 2/3/4
    //
    // Case 2: Sibling w is BLACK, both of w's children are BLACK
    //   → Color w RED (absorb one black from w), push double-black up to parent
    //
    // Case 3: Sibling w is BLACK, w's far child is BLACK, w's near child is RED
    //   → Recolor w's near child BLACK, w RED; rotate w away from x
    //   → Converts to Case 4
    //
    // Case 4: Sibling w is BLACK, w's far child is RED
    //   → Recolor w = parent's color, parent BLACK, w's far child BLACK
    //   → Rotate parent toward x
    //   → Double-black resolved; done.
    void fix_delete(RBNode* x) {
        while (x != root_ && x->color == BLACK) {
            if (x == x->parent->left) {
                RBNode* w = x->parent->right;  // sibling of x

                if (w->color == RED) {
                    // Case 1: red sibling → convert to Case 2/3/4
                    w->color           = BLACK;
                    x->parent->color   = RED;
                    left_rotate(x->parent);
                    w = x->parent->right;    // new sibling after rotation
                }

                if (w->left->color == BLACK && w->right->color == BLACK) {
                    // Case 2: both nephews black → push double-black up
                    w->color = RED;
                    x = x->parent;
                } else {
                    if (w->right->color == BLACK) {
                        // Case 3: far nephew BLACK, near nephew RED → rotate to Case 4
                        w->left->color = BLACK;
                        w->color       = RED;
                        right_rotate(w);
                        w = x->parent->right;
                    }
                    // Case 4: far nephew is RED → final fix
                    w->color         = x->parent->color;
                    x->parent->color = BLACK;
                    w->right->color  = BLACK;
                    left_rotate(x->parent);
                    x = root_;  // done
                }
            } else {
                // Mirror cases (x is right child)
                RBNode* w = x->parent->left;

                if (w->color == RED) {
                    w->color           = BLACK;
                    x->parent->color   = RED;
                    right_rotate(x->parent);
                    w = x->parent->left;
                }

                if (w->right->color == BLACK && w->left->color == BLACK) {
                    w->color = RED;
                    x = x->parent;
                } else {
                    if (w->left->color == BLACK) {
                        w->right->color = BLACK;
                        w->color        = RED;
                        left_rotate(w);
                        w = x->parent->left;
                    }
                    w->color         = x->parent->color;
                    x->parent->color = BLACK;
                    w->left->color   = BLACK;
                    right_rotate(x->parent);
                    x = root_;
                }
            }
        }
        x->color = BLACK;  // root is always BLACK; also resolves a red x
    }

    // ── helpers ───────────────────────────────────────────────────────────────

    RBNode* minimum(RBNode* node) const {
        while (node->left != nil_) node = node->left;
        return node;
    }

    RBNode* search_node(RBNode* node, int key) const {
        while (node != nil_ && node->key != key)
            node = (key < node->key) ? node->left : node->right;
        return node;
    }

    void inorder_helper(RBNode* node, std::vector<int>& result) const {
        if (node == nil_) return;
        inorder_helper(node->left, result);
        result.push_back(node->key);
        inorder_helper(node->right, result);
    }

    int height_helper(RBNode* node) const {
        if (node == nil_) return 0;
        return 1 + std::max(height_helper(node->left), height_helper(node->right));
    }

    int black_height_helper(RBNode* node) const {
        if (node == nil_) return 1;  // nil counts as 1 black node
        int lbh = black_height_helper(node->left);
        return (node->color == BLACK ? 1 : 0) + lbh;
    }

    // Verify: 1=every node red or black (trivially true in C++)
    //         3=no consecutive reds, 4=equal black-heights on all paths
    bool verify_helper(RBNode* node, int black_count, int& expected_bh) const {
        if (node == nil_) {
            if (expected_bh == -1) expected_bh = black_count;
            else if (expected_bh != black_count) {
                std::cerr << "[FAIL] Property 4: unequal black heights "
                          << expected_bh << " vs " << black_count << "\n";
                return false;
            }
            return true;
        }
        // Property 3: no red node has a red parent
        if (node->color == RED &&
            (node->left->color == RED || node->right->color == RED)) {
            std::cerr << "[FAIL] Property 3: consecutive red nodes near key="
                      << node->key << "\n";
            return false;
        }
        int bc = black_count + (node->color == BLACK ? 1 : 0);
        return verify_helper(node->left, bc, expected_bh) &&
               verify_helper(node->right, bc, expected_bh);
    }

    void destroy(RBNode* node) {
        if (node == nil_) return;
        destroy(node->left);
        destroy(node->right);
        delete node;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main: demonstrations
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Lab 4 — Red-Black Tree ===\n"
              << "    Indrajeet Yadav | 23BCS10199\n\n";

    RedBlackTree rbt;

    // ── Demo 1: Insert and verify ─────────────────────────────────────────────
    std::cout << "── Inserting: 10 20 30 15 25 5 1 35 28 ──\n\n";
    for (int k : {10, 20, 30, 15, 25, 5, 1, 35, 28})
        rbt.insert(k);

    std::cout << "Tree (level-order, R=Red B=Black):\n";
    rbt.print_tree();

    auto seq = rbt.inorder();
    std::cout << "\nInorder (must be sorted): ";
    for (int v : seq) std::cout << v << " ";
    std::cout << "\n";
    std::cout << "Size: " << rbt.size() << "\n";
    std::cout << "Height: " << rbt.height()
              << "  (upper bound 2*log2(" << rbt.size()+1 << ")≈"
              << 2*std::log2(rbt.size()+1) << ")\n";
    std::cout << "Black-height: " << rbt.black_height() << "\n";
    std::cout << "All RB properties satisfied: "
              << (rbt.verify() ? "YES" : "NO") << "\n\n";

    // ── Demo 2: Search ────────────────────────────────────────────────────────
    std::cout << "── Search ──\n";
    for (int k : {15, 28, 99, 5}) {
        std::cout << "  search(" << k << ") = "
                  << (rbt.search(k) ? "found" : "not found") << "\n";
    }
    std::cout << "\n";

    // ── Demo 3: Delete and verify ─────────────────────────────────────────────
    std::cout << "── Deleting: 20, 1, 30 ──\n\n";
    rbt.remove(20); rbt.remove(1); rbt.remove(30);

    rbt.print_tree();
    seq = rbt.inorder();
    std::cout << "\nInorder after deletions: ";
    for (int v : seq) std::cout << v << " ";
    std::cout << "\n";
    std::cout << "All RB properties satisfied: "
              << (rbt.verify() ? "YES" : "NO") << "\n\n";

    // ── Demo 4: Large insertion to verify O(log n) height ─────────────────────
    std::cout << "── Inserting 1..100, verifying O(log n) height ──\n";
    RedBlackTree rbt2;
    for (int i = 1; i <= 100; i++) rbt2.insert(i);
    std::cout << "  n=100, height=" << rbt2.height()
              << ", 2*log2(101)≈" << std::fixed << std::setprecision(1)
              << 2*std::log2(101) << "\n";
    std::cout << "  All RB properties: " << (rbt2.verify() ? "YES" : "NO") << "\n";

    RedBlackTree rbt3;
    for (int i = 1; i <= 10000; i++) rbt3.insert(i);
    std::cout << "  n=10000, height=" << rbt3.height()
              << ", 2*log2(10001)≈" << 2*std::log2(10001) << "\n";
    std::cout << "  All RB properties: " << (rbt3.verify() ? "YES" : "NO") << "\n\n";

    std::cout << "── Summary ──\n"
              << "  Insert/search/delete: O(log n) — guaranteed by RB invariants.\n"
              << "  The height is bounded to ≤ 2*log2(n+1) because:\n"
              << "    - Every path has the same black-height (Property 4).\n"
              << "    - No two consecutive RED nodes (Property 3).\n"
              << "    - Therefore no path can be more than twice as long as any other.\n"
              << "  Used in: Linux kernel (std::map analogue), Java TreeMap,\n"
              << "           in-memory database indexes, STL set/map internals.\n";

    return 0;
}
