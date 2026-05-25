// Lab 5 — Red-Black Tree
// Arjun, 24BCS10109
//
// A self-balancing binary search tree where every operation (insert,
// delete, lookup) runs in O(log n) because the tree's height is kept
// within 2 * log2(n + 1).
//
// The implementation follows the classic CLRS presentation but uses a
// real `nullptr` for missing children rather than a sentinel `NIL`
// node — both forms are correct; nullptrs make the code shorter to
// read.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

namespace rb {

enum class Color : std::uint8_t { Red, Black };

template <typename K>
struct Node {
    K     key;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    explicit Node(const K& k)
        : key(k), color(Color::Red),
          left(nullptr), right(nullptr), parent(nullptr) {}
};

template <typename K>
class RedBlackTree {
public:
    RedBlackTree() = default;
    ~RedBlackTree() { destroy(root_); }

    RedBlackTree(const RedBlackTree&)            = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    // Returns false if the key was already present.
    bool insert(const K& key) {
        Node<K>* parent  = nullptr;
        Node<K>* cursor  = root_;
        while (cursor) {
            parent = cursor;
            if      (key < cursor->key) cursor = cursor->left;
            else if (cursor->key < key) cursor = cursor->right;
            else                        return false;          // duplicate
        }

        auto* node    = new Node<K>(key);
        node->parent  = parent;
        if      (!parent)              root_ = node;
        else if (key < parent->key)    parent->left  = node;
        else                           parent->right = node;

        ++size_;
        fixupAfterInsert(node);
        return true;
    }

    bool contains(const K& key) const {
        for (Node<K>* n = root_; n; ) {
            if      (key < n->key) n = n->left;
            else if (n->key < key) n = n->right;
            else                   return true;
        }
        return false;
    }

    std::size_t size()   const noexcept { return size_; }
    int         height() const          { return heightOf(root_); }

    std::vector<K> inorder() const {
        std::vector<K> out;
        inorderInto(root_, out);
        return out;
    }

    // ASCII picture of the tree: each line is "K(R)" / "K(B)" indented
    // by depth. Right subtree printed above the key, left subtree
    // below, so reading from top to bottom is a clockwise tour.
    void prettyPrint(std::ostream& os) const {
        if (!root_) { os << "<empty>\n"; return; }
        prettyPrintRec(root_, "", true, os);
    }

    // Verifies all five RB invariants. Returns "" if valid, otherwise
    // a short reason string.
    std::string verifyInvariants() const {
        if (!root_) return "";
        if (root_->color != Color::Black)
            return "root is not black";
        int unused = 0;
        return verifyRec(root_, unused);
    }

private:
    Node<K>*    root_ = nullptr;
    std::size_t size_ = 0;

    static Color colorOf(const Node<K>* n) {
        return n ? n->color : Color::Black;   // nullptr leaves are black
    }

    void rotateLeft(Node<K>* x) {
        Node<K>* y  = x->right;
        x->right    = y->left;
        if (y->left) y->left->parent = x;
        y->parent   = x->parent;
        if      (!x->parent)              root_ = y;
        else if (x == x->parent->left)    x->parent->left  = y;
        else                              x->parent->right = y;
        y->left   = x;
        x->parent = y;
    }

    void rotateRight(Node<K>* x) {
        Node<K>* y  = x->left;
        x->left     = y->right;
        if (y->right) y->right->parent = x;
        y->parent   = x->parent;
        if      (!x->parent)              root_ = y;
        else if (x == x->parent->right)   x->parent->right = y;
        else                              x->parent->left  = y;
        y->right  = x;
        x->parent = y;
    }

    // CLRS RB-Insert-Fixup. The new node z is always red; we walk up
    // closing red-red violations until either the parent is black or
    // z becomes the root.
    void fixupAfterInsert(Node<K>* z) {
        while (z->parent && z->parent->color == Color::Red) {
            Node<K>* parent      = z->parent;
            Node<K>* grandparent = parent->parent;
            // The grandparent must exist: a red parent cannot be the
            // root (the root is black), so it has a parent.

            if (parent == grandparent->left) {
                Node<K>* uncle = grandparent->right;
                if (colorOf(uncle) == Color::Red) {
                    // Case 1: uncle is red → recolor and recurse upward.
                    parent->color      = Color::Black;
                    uncle->color       = Color::Black;
                    grandparent->color = Color::Red;
                    z = grandparent;
                } else {
                    if (z == parent->right) {
                        // Case 2: zig-zag → rotate parent into a line.
                        z = parent;
                        rotateLeft(z);
                        parent      = z->parent;
                        grandparent = parent->parent;
                    }
                    // Case 3: line shape → rotate grandparent, recolor.
                    parent->color      = Color::Black;
                    grandparent->color = Color::Red;
                    rotateRight(grandparent);
                }
            } else {
                // Mirror image of the left-side cases.
                Node<K>* uncle = grandparent->left;
                if (colorOf(uncle) == Color::Red) {
                    parent->color      = Color::Black;
                    uncle->color       = Color::Black;
                    grandparent->color = Color::Red;
                    z = grandparent;
                } else {
                    if (z == parent->left) {
                        z = parent;
                        rotateRight(z);
                        parent      = z->parent;
                        grandparent = parent->parent;
                    }
                    parent->color      = Color::Black;
                    grandparent->color = Color::Red;
                    rotateLeft(grandparent);
                }
            }
        }
        root_->color = Color::Black;
    }

    static int heightOf(const Node<K>* n) {
        if (!n) return 0;
        return 1 + std::max(heightOf(n->left), heightOf(n->right));
    }

    static void inorderInto(const Node<K>* n, std::vector<K>& out) {
        if (!n) return;
        inorderInto(n->left, out);
        out.push_back(n->key);
        inorderInto(n->right, out);
    }

    void destroy(Node<K>* n) {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }

    static void prettyPrintRec(const Node<K>* n,
                               const std::string& prefix,
                               bool is_root,
                               std::ostream& os) {
        if (!n) return;
        prettyPrintRec(n->right, prefix + (is_root ? "    " : "    "),
                       false, os);
        os << prefix
           << (is_root ? "" : "+-- ")
           << n->key
           << (n->color == Color::Red ? "(R)" : "(B)")
           << "\n";
        prettyPrintRec(n->left,  prefix + (is_root ? "    " : "    "),
                       false, os);
    }

    // Verifies:
    //   (1) every node is red or black,
    //   (2) the root is black                       — checked in caller,
    //   (3) every nullptr leaf counts as black,
    //   (4) a red node never has a red child,
    //   (5) every root-to-leaf path has the same black-height.
    // Returns "" on success; otherwise an explanation. blackHeight is
    // an out-parameter giving the black-height of this subtree.
    static std::string verifyRec(const Node<K>* n, int& blackHeight) {
        if (!n) { blackHeight = 1; return ""; }
        if (n->color == Color::Red) {
            if (colorOf(n->left)  == Color::Red ||
                colorOf(n->right) == Color::Red)
                return "red node with red child";
        }
        int leftBH = 0, rightBH = 0;
        std::string e1 = verifyRec(n->left,  leftBH);
        if (!e1.empty()) return e1;
        std::string e2 = verifyRec(n->right, rightBH);
        if (!e2.empty()) return e2;
        if (leftBH != rightBH)
            return "black-height mismatch between left and right";
        blackHeight = leftBH + (n->color == Color::Black ? 1 : 0);
        return "";
    }
};

} // namespace rb

// ---------------------------------------------------------------------
// Demo
// ---------------------------------------------------------------------

static void demoInOrderInsertion() {
    std::cout << "\n--- Demo 1: insert 1..10 in order ---\n";
    rb::RedBlackTree<int> t;
    for (int i = 1; i <= 10; ++i) t.insert(i);
    t.prettyPrint(std::cout);
    std::cout << "size   = " << t.size()   << "\n";
    std::cout << "height = " << t.height() << "  (theoretical max for 10 keys: "
              << "2*log2(11) \xE2\x89\x88 6.9)\n";
    auto problem = t.verifyInvariants();
    std::cout << "invariants: "
              << (problem.empty() ? "OK" : problem) << "\n";
}

static void demoRandomKeys() {
    std::cout << "\n--- Demo 2: 15 unsorted keys ---\n";
    const std::vector<int> keys = {
        20, 4, 15, 70, 50, 80, 65, 25, 5, 60, 30, 10, 45, 75, 35
    };
    rb::RedBlackTree<int> t;
    for (int k : keys) t.insert(k);

    t.prettyPrint(std::cout);
    std::cout << "size   = " << t.size()   << "\n";
    std::cout << "height = " << t.height() << "\n";

    std::cout << "inorder:";
    for (int k : t.inorder()) std::cout << " " << k;
    std::cout << "\n";

    std::cout << "contains(60)  -> " << (t.contains(60)  ? "yes" : "no") << "\n";
    std::cout << "contains(100) -> " << (t.contains(100) ? "yes" : "no") << "\n";

    auto problem = t.verifyInvariants();
    std::cout << "invariants: "
              << (problem.empty() ? "OK" : problem) << "\n";
}

static void demoHeightBound() {
    std::cout << "\n--- Demo 3: 10 000 sequential inserts ---\n";
    rb::RedBlackTree<int> t;
    for (int i = 0; i < 10000; ++i) t.insert(i);
    std::cout << "size   = " << t.size()   << "\n";
    std::cout << "height = " << t.height()
              << "  (bound 2*log2(10001) \xE2\x89\x88 26.6)\n";
    auto problem = t.verifyInvariants();
    std::cout << "invariants: "
              << (problem.empty() ? "OK" : problem) << "\n";
}

int main() {
    std::cout << "Red-Black Tree demo — Arjun, 24BCS10109\n";
    demoInOrderInsertion();
    demoRandomKeys();
    demoHeightBound();
    return 0;
}
