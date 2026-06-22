/*
 * =============================================================================
 *  Red-Black Tree (CLRS Chapter 13)
 * =============================================================================
 *
 *  Course  : Advanced DBMS (Scaler)
 *  Author  : Praveen Kumar
 *  Date    : 2026-05-25
 *
 *  Purpose : Implement a red-black tree with int keys and string values.
 *            Supports insert, erase, search, and in-order traversal.
 *            Every mutation is followed by an invariant check so that
 *            any logic bug is caught immediately.
 *
 *  The five red-black properties (from CLRS):
 *    1. Every node is RED or BLACK.
 *    2. The root is BLACK.
 *    3. Every NIL leaf is BLACK.
 *    4. No RED node has a RED child.
 *    5. Every root-to-NIL path has the same number of BLACK nodes.
 *
 *  Build   : g++ -std=c++17 -O2 -o rb_tree rb_tree.cpp
 *  Run     : ./rb_tree
 * =============================================================================
 */

#include <iostream>
#include <string>
#include <functional>
#include <stdexcept>
#include <cassert>

/* ===========================================================================
 *  Colour and Node
 * =========================================================================== */

enum Colour { RED, BLACK };

struct Node {
    int         key;
    std::string value;
    Colour      colour;
    Node       *left, *right, *parent;

    Node(int k, std::string v, Colour c, Node *nil)
        : key(k), value(std::move(v)), colour(c),
          left(nil), right(nil), parent(nil) {}
};


/* ===========================================================================
 *  RBTree
 * ===========================================================================
 *
 *  Uses a single shared NIL sentinel node (all leaves point to it).
 *  The sentinel is always BLACK; its key/value fields are ignored.
 * =========================================================================== */

class RBTree {
public:
    RBTree()
    {
        nil_          = new Node(0, "", BLACK, nullptr);
        nil_->left    = nil_;
        nil_->right   = nil_;
        nil_->parent  = nil_;
        root_         = nil_;
    }

    ~RBTree() { clear(root_); delete nil_; }

    /* -----------------------------------------------------------------------
     *  insert -- BST insert then fix up colour violations.
     *
     *  CLRS insert fix-up has three cases (plus mirror images):
     *    Case 1: Uncle is RED -> recolour, move problem to grandparent.
     *    Case 2: Uncle is BLACK, z is inner grandchild -> rotate to make
     *            it an outer grandchild (falls through to Case 3).
     *    Case 3: Uncle is BLACK, z is outer grandchild -> recolour + rotate.
     * ----------------------------------------------------------------------- */
    void insert(int key, const std::string &value)
    {
        Node *z = new Node(key, value, RED, nil_);

        /* Standard BST insert */
        Node *y = nil_;
        Node *x = root_;
        while (x != nil_) {
            y = x;
            if (key < x->key)       x = x->left;
            else if (key > x->key)  x = x->right;
            else {
                /* Key already exists -- update value and bail. */
                x->value = value;
                delete z;
                return;
            }
        }
        z->parent = y;
        if (y == nil_)            root_ = z;
        else if (key < y->key)    y->left  = z;
        else                      y->right = z;

        fix_insert(z);
    }

    /* -----------------------------------------------------------------------
     *  erase -- BST splice then fix up black-height deficit.
     *
     *  CLRS delete fix-up has four cases (plus mirror images):
     *    Case 1: Sibling is RED -> rotate to make sibling BLACK.
     *    Case 2: Sibling BLACK with two BLACK children -> push deficit up.
     *    Case 3: Sibling BLACK, outer nephew BLACK, inner nephew RED ->
     *            rotate to convert to Case 4.
     *    Case 4: Sibling BLACK, outer nephew RED -> rotate + recolour. Done.
     * ----------------------------------------------------------------------- */
    bool erase(int key)
    {
        Node *z = find_node(key);
        if (z == nil_) return false;
        delete_node(z);
        return true;
    }

    /* -----------------------------------------------------------------------
     *  search -- O(log n) key lookup.
     * ----------------------------------------------------------------------- */
    bool search(int key, std::string &out_value) const
    {
        Node *n = find_node(key);
        if (n == nil_) return false;
        out_value = n->value;
        return true;
    }

    bool contains(int key) const { return find_node(key) != nil_; }

    /* -----------------------------------------------------------------------
     *  in_order -- visit all nodes in ascending key order.
     * ----------------------------------------------------------------------- */
    void in_order(std::function<void(int, const std::string &)> fn) const
    {
        in_order_impl(root_, fn);
    }

    /* -----------------------------------------------------------------------
     *  check_invariants -- verifies all 5 RB properties.
     *  Returns "" if healthy, or a description of the first violation.
     * ----------------------------------------------------------------------- */
    std::string check_invariants() const
    {
        if (root_ == nil_) return "";
        if (root_->colour != BLACK) return "Property 2 violation: root is not BLACK";
        int bh = -1;
        return check_node(root_, 0, bh);
    }

    int size() const { return size_impl(root_); }

private:
    Node *nil_;
    Node *root_;

    /* ---- Rotations -------------------------------------------------------- */

    /*
     * rotate_left(x):
     *
     *       x                 y
     *      / \               / \
     *     a   y    -->      x   c
     *        / \           / \
     *       b   c         a   b
     */
    void rotate_left(Node *x)
    {
        Node *y  = x->right;
        x->right = y->left;
        if (y->left != nil_) y->left->parent = x;

        y->parent = x->parent;
        if (x->parent == nil_)        root_           = y;
        else if (x == x->parent->left) x->parent->left = y;
        else                            x->parent->right = y;

        y->left   = x;
        x->parent = y;
    }

    /*
     * rotate_right(y):
     *
     *         y             x
     *        / \           / \
     *       x   c  -->    a   y
     *      / \               / \
     *     a   b             b   c
     */
    void rotate_right(Node *y)
    {
        Node *x  = y->left;
        y->left  = x->right;
        if (x->right != nil_) x->right->parent = y;

        x->parent = y->parent;
        if (y->parent == nil_)         root_            = x;
        else if (y == y->parent->right) y->parent->right = x;
        else                             y->parent->left  = x;

        x->right  = y;
        y->parent = x;
    }

    /* ---- transplant -- replace subtree u with subtree v ------------------- */
    void transplant(Node *u, Node *v)
    {
        if (u->parent == nil_)         root_            = v;
        else if (u == u->parent->left)  u->parent->left  = v;
        else                             u->parent->right = v;
        v->parent = u->parent;
    }

    /* ---- fix_insert ------------------------------------------------------- */
    void fix_insert(Node *z)
    {
        while (z->parent->colour == RED) {
            if (z->parent == z->parent->parent->left) {
                Node *y = z->parent->parent->right;   /* uncle */

                if (y->colour == RED) {
                    /* Case 1: uncle is RED */
                    z->parent->colour          = BLACK;
                    y->colour                  = BLACK;
                    z->parent->parent->colour  = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        /* Case 2: z is right child -- rotate to make left */
                        z = z->parent;
                        rotate_left(z);
                    }
                    /* Case 3: z is left child */
                    z->parent->colour         = BLACK;
                    z->parent->parent->colour = RED;
                    rotate_right(z->parent->parent);
                }
            } else {
                /* Mirror: parent is right child */
                Node *y = z->parent->parent->left;    /* uncle */

                if (y->colour == RED) {
                    /* Case 1 (mirror) */
                    z->parent->colour         = BLACK;
                    y->colour                 = BLACK;
                    z->parent->parent->colour = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        /* Case 2 (mirror) */
                        z = z->parent;
                        rotate_right(z);
                    }
                    /* Case 3 (mirror) */
                    z->parent->colour         = BLACK;
                    z->parent->parent->colour = RED;
                    rotate_left(z->parent->parent);
                }
            }
        }
        root_->colour = BLACK;   /* Property 2 */
    }

    /* ---- delete_node ------------------------------------------------------ */
    void delete_node(Node *z)
    {
        Node   *y            = z;
        Node   *x            = nil_;
        Colour  y_orig_colour = y->colour;

        if (z->left == nil_) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == nil_) {
            x = z->left;
            transplant(z, z->left);
        } else {
            /* Two real children: replace z with its in-order successor y. */
            y = minimum(z->right);
            y_orig_colour = y->colour;
            x = y->right;

            if (y->parent == z) {
                x->parent = y;   /* x may be nil_; keep parent set for fix_erase */
            } else {
                transplant(y, y->right);
                y->right         = z->right;
                y->right->parent = y;
            }
            transplant(z, y);
            y->left          = z->left;
            y->left->parent  = y;
            y->colour        = z->colour;
        }

        delete z;

        if (y_orig_colour == BLACK) {
            fix_erase(x);
        }

        nil_->parent = nil_;   /* reset sentinel -- fix_erase may have set it */
    }

    /* ---- fix_erase -------------------------------------------------------- */
    void fix_erase(Node *x)
    {
        while (x != root_ && x->colour == BLACK) {
            if (x == x->parent->left) {
                Node *w = x->parent->right;  /* sibling */

                if (w->colour == RED) {
                    /* Case 1: sibling is RED */
                    w->colour            = BLACK;
                    x->parent->colour    = RED;
                    rotate_left(x->parent);
                    w = x->parent->right;
                }

                if (w->left->colour == BLACK && w->right->colour == BLACK) {
                    /* Case 2: sibling BLACK with two BLACK children */
                    w->colour = RED;
                    x = x->parent;
                } else {
                    if (w->right->colour == BLACK) {
                        /* Case 3: sibling BLACK, outer nephew BLACK */
                        w->left->colour = BLACK;
                        w->colour       = RED;
                        rotate_right(w);
                        w = x->parent->right;
                    }
                    /* Case 4: sibling BLACK, outer nephew RED */
                    w->colour            = x->parent->colour;
                    x->parent->colour    = BLACK;
                    w->right->colour     = BLACK;
                    rotate_left(x->parent);
                    x = root_;
                }
            } else {
                /* Mirror */
                Node *w = x->parent->left;

                if (w->colour == RED) {
                    w->colour            = BLACK;
                    x->parent->colour    = RED;
                    rotate_right(x->parent);
                    w = x->parent->left;
                }

                if (w->right->colour == BLACK && w->left->colour == BLACK) {
                    w->colour = RED;
                    x = x->parent;
                } else {
                    if (w->left->colour == BLACK) {
                        w->right->colour = BLACK;
                        w->colour        = RED;
                        rotate_left(w);
                        w = x->parent->left;
                    }
                    w->colour            = x->parent->colour;
                    x->parent->colour    = BLACK;
                    w->left->colour      = BLACK;
                    rotate_right(x->parent);
                    x = root_;
                }
            }
        }
        x->colour = BLACK;
    }

    /* ---- helpers ---------------------------------------------------------- */

    Node *minimum(Node *x) const
    {
        while (x->left != nil_) x = x->left;
        return x;
    }

    Node *find_node(int key) const
    {
        Node *x = root_;
        while (x != nil_) {
            if (key == x->key) return x;
            x = (key < x->key) ? x->left : x->right;
        }
        return nil_;
    }

    void in_order_impl(Node *x, std::function<void(int, const std::string &)> &fn) const
    {
        if (x == nil_) return;
        in_order_impl(x->left,  fn);
        fn(x->key, x->value);
        in_order_impl(x->right, fn);
    }

    int size_impl(Node *x) const
    {
        if (x == nil_) return 0;
        return 1 + size_impl(x->left) + size_impl(x->right);
    }

    void clear(Node *x)
    {
        if (x == nil_) return;
        clear(x->left);
        clear(x->right);
        delete x;
    }

    /* check_node: returns "" on success, error string on first violation.
       bh tracks expected black-height (set on first path). */
    std::string check_node(Node *x, int current_bh, int &expected_bh) const
    {
        if (x == nil_) {
            current_bh++;
            if (expected_bh == -1) expected_bh = current_bh;
            else if (current_bh != expected_bh)
                return "Property 5: unequal black-heights";
            return "";
        }

        /* Property 4: no two consecutive reds */
        if (x->colour == RED) {
            if (x->left->colour == RED)
                return "Property 4: left child of RED node is RED (key=" + std::to_string(x->key) + ")";
            if (x->right->colour == RED)
                return "Property 4: right child of RED node is RED (key=" + std::to_string(x->key) + ")";
        }

        int bh = current_bh + (x->colour == BLACK ? 1 : 0);

        std::string err = check_node(x->left, bh, expected_bh);
        if (!err.empty()) return err;
        return check_node(x->right, bh, expected_bh);
    }
};


/* ===========================================================================
 *  main -- demo workload
 * =========================================================================== */

static void check(const RBTree &t, const std::string &phase)
{
    std::string err = t.check_invariants();
    if (!err.empty()) {
        std::cerr << "INVARIANT FAIL after " << phase << ": " << err << "\n";
        std::exit(1);
    }
}

int main()
{
    std::cout << "============================================================\n";
    std::cout << "  Red-Black Tree (CLRS chapter 13)\n";
    std::cout << "============================================================\n\n";

    RBTree t;

    /* ---  Phase 1: Insert 15 keys --- */
    std::cout << "[PHASE 1] Insert\n";
    std::cout << "------------------------------------------------------------\n";
    int keys[] = {41, 38, 31, 12, 19, 8, 55, 45, 63, 74, 25, 17, 3, 50, 60};
    std::string books[] = {
        "Database Internals", "DDIA", "OSTEP", "CLRS", "Clean Code",
        "Linux Programming Interface", "TCP/IP Illustrated", "C Programming Language",
        "Modern Operating Systems", "Computer Networks", "Pragmatic Programmer",
        "Compilers (Dragon Book)", "SICP", "UNIX Network Programming", "CS:APP"
    };

    for (int i = 0; i < 15; ++i) {
        t.insert(keys[i], books[i]);
        std::cout << "  insert(" << keys[i] << ", \"" << books[i] << "\")\n";
    }
    check(t, "bulk insert");
    std::cout << "  size = " << t.size() << "  [invariants OK]\n\n";

    /* --- Phase 2: In-order traversal --- */
    std::cout << "[PHASE 2] In-order traversal (should be ascending)\n";
    std::cout << "------------------------------------------------------------\n";
    t.in_order([](int k, const std::string &v) {
        std::cout << "  " << k << " -> " << v << "\n";
    });
    std::cout << "\n";

    /* --- Phase 3: Search --- */
    std::cout << "[PHASE 3] Search\n";
    std::cout << "------------------------------------------------------------\n";
    std::string val;
    for (int k : {3, 41, 74, 99}) {
        bool found = t.search(k, val);
        std::cout << "  search(" << k << ") -> ";
        if (found) std::cout << "found: \"" << val << "\"\n";
        else        std::cout << "not found\n";
    }
    std::cout << "\n";

    /* --- Phase 4: Erase --- */
    std::cout << "[PHASE 4] Erase\n";
    std::cout << "------------------------------------------------------------\n";
    for (int k : {12, 38, 55, 3, 74}) {
        bool ok = t.erase(k);
        std::cout << "  erase(" << k << ") -> " << (ok ? "removed" : "not found") << "\n";
        check(t, "erase(" + std::to_string(k) + ")");
    }
    std::cout << "  size = " << t.size() << "  [invariants OK after all erases]\n\n";

    /* --- Phase 5: In-order after erase --- */
    std::cout << "[PHASE 5] In-order after erase\n";
    std::cout << "------------------------------------------------------------\n";
    t.in_order([](int k, const std::string &v) {
        std::cout << "  " << k << " -> " << v << "\n";
    });
    std::cout << "\n";

    /* --- Phase 6: Overwrite existing key --- */
    std::cout << "[PHASE 6] Overwrite\n";
    std::cout << "------------------------------------------------------------\n";
    int sz_before = t.size();
    t.insert(41, "Database Internals (2nd read)");
    t.search(41, val);
    std::cout << "  After overwrite(41): \"" << val << "\"\n";
    std::cout << "  size unchanged: " << (t.size() == sz_before ? "YES" : "NO") << "\n\n";

    check(t, "overwrite");

    std::cout << "============================================================\n";
    std::cout << "  All checks passed.\n";
    std::cout << "============================================================\n";

    return 0;
}
