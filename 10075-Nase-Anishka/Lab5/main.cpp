// Lab 5 — Red-Black Tree
// Author: Nase Anishka (Roll No. 10075)
//
// A templated Red-Black Tree with insert, contains, in-order traversal,
// a tree-shape pretty printer, and a validator that asserts all five
// of the RB invariants.
//
// Build:   cmake -B build && cmake --build build
// Run:     ./build/rb_tree

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

template <typename T>
class RedBlackTree {
public:
    RedBlackTree() = default;
    ~RedBlackTree() { destroy(root_); }

    RedBlackTree(const RedBlackTree&) = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    void insert(const T& value) {
        Node* parent = nullptr;
        Node* cursor = root_;
        while (cursor) {
            parent = cursor;
            if (value < cursor->value) cursor = cursor->left;
            else if (cursor->value < value) cursor = cursor->right;
            else return; // already present, RB tree of unique keys
        }

        Node* fresh = new Node(value, Color::Red, parent);
        if (!parent) {
            root_ = fresh;
        } else if (value < parent->value) {
            parent->left = fresh;
        } else {
            parent->right = fresh;
        }
        ++size_;

        rebalanceAfterInsert(fresh);
    }

    bool contains(const T& value) const {
        for (Node* n = root_; n; ) {
            if (value < n->value)      n = n->left;
            else if (n->value < value) n = n->right;
            else                       return true;
        }
        return false;
    }

    std::size_t size()  const { return size_; }
    bool        empty() const { return root_ == nullptr; }
    std::size_t height() const { return heightOf(root_); }
    std::size_t blackHeight() const { return blackHeightOf(root_); }

    // Calls `fn(value)` for every key in ascending order.
    template <typename Fn>
    void inOrder(Fn fn) const { inOrderHelper(root_, fn); }

    // Visual ASCII tree, drawn sideways. Right subtrees are above, left
    // subtrees are below, parent in the middle -- the conventional
    // "tilt the tree 90 degrees clockwise" view.
    void prettyPrint(std::ostream& os = std::cout) const {
        if (!root_) { os << "(empty)\n"; return; }
        prettyPrintHelper(root_, "", true, os);
    }

    // Walks the tree and throws if any RB invariant is broken.
    // Returns the black-height so the caller can print it.
    std::size_t validate() const {
        if (root_ && root_->color == Color::Red) {
            throw std::logic_error("invariant 2 broken: root is red");
        }
        return validateHelper(root_);
    }

private:
    enum class Color : unsigned char { Red, Black };

    struct Node {
        T value;
        Color color;
        Node* parent;
        Node* left{nullptr};
        Node* right{nullptr};

        Node(const T& v, Color c, Node* p) : value(v), color(c), parent(p) {}
    };

    Node* root_{nullptr};
    std::size_t size_{0};

    // --- structural helpers -------------------------------------------------

    static Color colorOf(const Node* n) {
        return n ? n->color : Color::Black; // missing children are black
    }

    Node* grandparent(Node* n) const {
        return (n && n->parent) ? n->parent->parent : nullptr;
    }

    Node* sibling(Node* n) const {
        if (!n || !n->parent) return nullptr;
        return (n == n->parent->left) ? n->parent->right : n->parent->left;
    }

    Node* uncle(Node* n) const {
        Node* gp = grandparent(n);
        if (!gp) return nullptr;
        return (n->parent == gp->left) ? gp->right : gp->left;
    }

    void rotateLeft(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if (!x->parent)            root_ = y;
        else if (x == x->parent->left)  x->parent->left  = y;
        else                            x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void rotateRight(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        y->parent = x->parent;
        if (!x->parent)            root_ = y;
        else if (x == x->parent->left)  x->parent->left  = y;
        else                            x->parent->right = y;
        y->right = x;
        x->parent = y;
    }

    // --- insert fixup -------------------------------------------------------
    //
    // Three cases per side; the side (parent is left/right of grandparent)
    // is mirrored so we handle it via a `parentIsLeft` flag.
    void rebalanceAfterInsert(Node* node) {
        while (node != root_ && colorOf(node->parent) == Color::Red) {
            Node* parent = node->parent;
            Node* gp     = grandparent(node);
            const bool parentIsLeft = (parent == gp->left);
            Node* u = parentIsLeft ? gp->right : gp->left;

            // Case 1: uncle is red -> recolor parent + uncle to black,
            // grandparent to red, then continue from the grandparent.
            if (colorOf(u) == Color::Red) {
                parent->color = Color::Black;
                u->color      = Color::Black;
                gp->color     = Color::Red;
                node = gp;
                continue;
            }

            // Case 2: node is "inner" (right child of left parent or vice
            // versa) -> rotate at the parent to turn it into Case 3.
            if (parentIsLeft && node == parent->right) {
                rotateLeft(parent);
                node   = parent;
                parent = node->parent;
            } else if (!parentIsLeft && node == parent->left) {
                rotateRight(parent);
                node   = parent;
                parent = node->parent;
            }

            // Case 3: node is "outer" -> recolor + single rotation at gp.
            parent->color = Color::Black;
            gp->color     = Color::Red;
            if (parentIsLeft) rotateRight(gp);
            else              rotateLeft(gp);
        }
        root_->color = Color::Black;
    }

    // --- traversal / display -----------------------------------------------

    template <typename Fn>
    void inOrderHelper(const Node* n, Fn& fn) const {
        if (!n) return;
        inOrderHelper(n->left, fn);
        fn(n->value);
        inOrderHelper(n->right, fn);
    }

    void prettyPrintHelper(const Node* n, const std::string& prefix,
                           bool isRoot, std::ostream& os) const {
        if (!n) return;
        prettyPrintHelper(n->right, prefix + (isRoot ? "    " : "        "),
                          false, os);
        if (!isRoot) {
            os << prefix << "    /----- ";
        } else {
            os << prefix;
        }
        os << n->value << (n->color == Color::Red ? "(R)" : "(B)") << '\n';
        prettyPrintHelper(n->left, prefix + (isRoot ? "    " : "        "),
                          false, os);
    }

    std::size_t heightOf(const Node* n) const {
        if (!n) return 0;
        return 1 + std::max(heightOf(n->left), heightOf(n->right));
    }

    std::size_t blackHeightOf(const Node* n) const {
        std::size_t bh = 0;
        for (const Node* c = n; c; c = c->left) {
            if (c->color == Color::Black) ++bh;
        }
        return bh;
    }

    // --- invariant checker -------------------------------------------------
    //
    // Returns the black-height of the subtree rooted at n.
    // Throws std::logic_error if anything is off.
    std::size_t validateHelper(const Node* n) const {
        if (!n) return 1; // null leaves count as black

        // Invariant 4: no two consecutive reds.
        if (n->color == Color::Red) {
            if (colorOf(n->left) == Color::Red ||
                colorOf(n->right) == Color::Red) {
                throw std::logic_error(
                    "invariant 4 broken: red node has a red child");
            }
        }

        // BST property: left < n < right.
        if (n->left && !(n->left->value < n->value)) {
            throw std::logic_error("BST property broken on left child");
        }
        if (n->right && !(n->value < n->right->value)) {
            throw std::logic_error("BST property broken on right child");
        }

        // Parent pointers are consistent.
        if (n->left  && n->left->parent  != n) {
            throw std::logic_error("parent pointer broken (left)");
        }
        if (n->right && n->right->parent != n) {
            throw std::logic_error("parent pointer broken (right)");
        }

        const std::size_t bhL = validateHelper(n->left);
        const std::size_t bhR = validateHelper(n->right);
        if (bhL != bhR) {
            throw std::logic_error(
                "invariant 5 broken: differing black-heights at "
                + std::to_string(n->value));
        }
        return bhL + (n->color == Color::Black ? 1 : 0);
    }

    void destroy(Node* n) {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }
};

// ----------------------------- demo -----------------------------------------

namespace {

void runScenario(const std::string& label,
                 const std::vector<int>& keys) {
    std::cout << "\n=== " << label << " ===\n";
    std::cout << "inserting: ";
    for (int k : keys) std::cout << k << ' ';
    std::cout << '\n';

    RedBlackTree<int> tree;
    for (int k : keys) tree.insert(k);

    std::cout << "size         : " << tree.size() << '\n';
    std::cout << "height       : " << tree.height() << '\n';
    std::cout << "black-height : " << tree.blackHeight() << '\n';

    std::cout << "in-order     :";
    tree.inOrder([](int v) { std::cout << ' ' << v; });
    std::cout << '\n';

    std::cout << "shape (right subtree on top, parent in middle):\n";
    tree.prettyPrint();

    tree.validate();
    std::cout << "validate()   : OK (all five RB invariants hold)\n";

    std::cout << "contains 15  : " << (tree.contains(15) ? "yes" : "no") << '\n';
    std::cout << "contains 99  : " << (tree.contains(99) ? "yes" : "no") << '\n';
}

}

int main() {
    std::cout << "Red-Black Tree demo\n";

    // Scenario 1: the same six keys as a sanity baseline -- but the resulting
    // tree shape is the point, not the keys themselves.
    runScenario("six mixed inserts",
                {10, 20, 30, 15, 5, 1});

    // Scenario 2: ascending input -- worst case for a plain BST, but the RB
    // rotations should keep height roughly 2 * log2(n).
    runScenario("ascending 1..10",
                {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});

    // Scenario 3: descending input -- mirror of scenario 2.
    runScenario("descending 10..1",
                {10, 9, 8, 7, 6, 5, 4, 3, 2, 1});

    // Scenario 4: a duplicate-insert is silently ignored.
    {
        std::cout << "\n=== duplicate insert is a no-op ===\n";
        RedBlackTree<int> t;
        t.insert(42);
        t.insert(42);
        t.insert(42);
        std::cout << "size after three insert(42) calls: " << t.size() << '\n';
    }

    return 0;
}
