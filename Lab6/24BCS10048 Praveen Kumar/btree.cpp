/*
 * =============================================================================
 *  B-Tree (CLRS Chapter 18)
 * =============================================================================
 *
 *  Course  : Advanced DBMS (Scaler)
 *  Author  : Praveen Kumar
 *  Date    : 2026-05-30
 *
 *  Purpose : Implement a B-tree of minimum degree t.  Every internal node
 *            holds between t-1 and 2t-1 keys.  Supports:
 *              - insert   (split on the way down -- preemptive split)
 *              - search   (returns pointer to node and index)
 *              - remove   (handles all 3 CLRS cases)
 *              - in-order display
 *
 *  In a DBMS context this is the structure that underlies InnoDB's clustered
 *  index, SQLite's table B-tree, and PostgreSQL's GiST/B-tree indexes.  The
 *  minimum degree t determines the page fill factor: with t=512 and 8 KB pages
 *  each node holds up to 1023 keys and a typical database is only 3-4 levels
 *  deep regardless of table size.
 *
 *  Build   : g++ -std=c++17 -O2 -Wall -Wextra -o btree btree.cpp
 *  Run     : ./btree
 * =============================================================================
 */

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <iomanip>

/* ===========================================================================
 *  BTreeNode
 * ===========================================================================
 *
 *  Each node stores:
 *    keys     : sorted vector of integer keys (up to 2t-1)
 *    children : child pointers (keys.size() + 1 for internal nodes)
 *    leaf     : true if this is a leaf node
 *
 *  Invariants (CLRS 18.1):
 *    1. Every node x has x.n keys stored in non-decreasing order.
 *    2. If x is internal it has x.n + 1 children.
 *    3. Keys in subtree i satisfy: child[i-1].max < key[i-1] < child[i].min
 *    4. All leaves have the same depth.
 *    5. t-1 <= x.n <= 2t-1  (except root: 1 <= root.n <= 2t-1)
 * -------------------------------------------------------------------------- */

struct BTreeNode {
    std::vector<int>        keys;
    std::vector<BTreeNode*> children;
    bool                    leaf;

    explicit BTreeNode(bool is_leaf) : leaf(is_leaf) {}

    ~BTreeNode()
    {
        for (BTreeNode *c : children) delete c;
    }

    /* Number of keys currently in this node. */
    int n() const { return static_cast<int>(keys.size()); }
};


/* ===========================================================================
 *  BTree
 * =========================================================================== */

class BTree {
public:
    /* t = minimum degree.
     * Each non-root node has at least t-1 and at most 2t-1 keys.
     * Each non-root internal node has at least t and at most 2t children. */
    explicit BTree(int t) : t_(t), root_(new BTreeNode(true)) {}

    ~BTree() { delete root_; }

    /* -----------------------------------------------------------------------
     *  search -- return {node, index} if found, {nullptr, -1} otherwise.
     *
     *  Complexity: O(t * log_t(n)) -- each level does a linear scan over
     *  at most 2t-1 keys.  With binary search in each node it is
     *  O(log(t) * log_t(n)) = O(log n).
     * ----------------------------------------------------------------------- */
    std::pair<BTreeNode*, int> search(int key) const
    {
        return search_node(root_, key);
    }

    bool contains(int key) const
    {
        auto [node, idx] = search(key);
        return node != nullptr;
    }

    /* -----------------------------------------------------------------------
     *  insert -- preemptive split variant (CLRS 18.3)
     *
     *  Split full nodes on the way DOWN so we never need to walk back up.
     *  A node is "full" when it has 2t-1 keys.
     * ----------------------------------------------------------------------- */
    void insert(int key)
    {
        BTreeNode *r = root_;

        if (r->n() == 2 * t_ - 1) {
            /* Root is full -- split it.  Tree grows taller by one level. */
            BTreeNode *s  = new BTreeNode(false);
            root_         = s;
            s->children.push_back(r);
            split_child(s, 0);
            insert_nonfull(s, key);
        } else {
            insert_nonfull(r, key);
        }
    }

    /* -----------------------------------------------------------------------
     *  remove -- CLRS 18.3, three main cases.
     *
     *  Case 1: key is in a leaf.  Delete directly.
     *  Case 2: key is in an internal node.
     *    2a: Left child has >= t keys: replace with predecessor, recurse.
     *    2b: Right child has >= t keys: replace with successor, recurse.
     *    2c: Both children have t-1 keys: merge, recurse into merged child.
     *  Case 3: key is not in current node.
     *    3a: Child that should contain key has only t-1 keys.
     *        Borrow from left or right sibling if possible, else merge.
     *    Recurse into the appropriate child.
     * ----------------------------------------------------------------------- */
    bool remove(int key)
    {
        if (root_->n() == 0) return false;
        bool removed = remove_from(root_, key);

        /* If root became empty after a merge, shrink the tree. */
        if (root_->n() == 0 && !root_->leaf) {
            BTreeNode *old_root = root_;
            root_ = root_->children[0];
            old_root->children.clear();
            delete old_root;
        }
        return removed;
    }

    /* -----------------------------------------------------------------------
     *  display -- in-order traversal (ascending key order)
     * ----------------------------------------------------------------------- */
    void display() const
    {
        std::cout << "  In-order: ";
        in_order(root_);
        std::cout << "\n";
    }

    /* -----------------------------------------------------------------------
     *  display_tree -- level-order dump showing tree structure
     * ----------------------------------------------------------------------- */
    void display_tree() const
    {
        std::cout << "  Tree structure (level-order):\n";
        print_level(root_, 0);
    }

    int min_degree() const { return t_; }

private:
    int        t_;
    BTreeNode *root_;

    /* ---- search_node ------------------------------------------------------ */
    std::pair<BTreeNode*, int> search_node(BTreeNode *x, int key) const
    {
        int i = 0;
        while (i < x->n() && key > x->keys[i]) ++i;

        if (i < x->n() && key == x->keys[i])
            return {x, i};

        if (x->leaf)
            return {nullptr, -1};

        return search_node(x->children[i], key);
    }

    /* ---- split_child ------------------------------------------------------
     *
     *  Split x->children[i] (which must be full: 2t-1 keys) into two nodes
     *  of t-1 keys each.  The median key is promoted into x.
     *
     *  Before:                 After:
     *    x: [...  ki  ...]       x: [...  ki  MED  ki+1  ...]
     *         |                         |        |
     *      child (full)              child(L)  child(R)
     * ---------------------------------------------------------------------- */
    void split_child(BTreeNode *x, int i)
    {
        BTreeNode *y = x->children[i];   /* the full child */
        BTreeNode *z = new BTreeNode(y->leaf);

        /* Move the right half of y's keys into z. */
        z->keys.assign(y->keys.begin() + t_, y->keys.end());
        y->keys.resize(t_ - 1);

        /* If y is internal, move the right half of its children into z. */
        if (!y->leaf) {
            z->children.assign(y->children.begin() + t_, y->children.end());
            y->children.resize(t_);
        }

        /* Insert z as a child of x, promote the median key. */
        x->children.insert(x->children.begin() + i + 1, z);
        x->keys.insert(x->keys.begin() + i, y->keys[t_ - 1]);
        y->keys.resize(t_ - 1);
    }

    /* ---- insert_nonfull ---------------------------------------------------
     *  Insert key into a subtree rooted at x, which is guaranteed not full.
     * ---------------------------------------------------------------------- */
    void insert_nonfull(BTreeNode *x, int key)
    {
        int i = x->n() - 1;

        if (x->leaf) {
            /* Shift keys right and insert. */
            x->keys.push_back(0);
            while (i >= 0 && key < x->keys[i]) {
                x->keys[i + 1] = x->keys[i];
                --i;
            }
            x->keys[i + 1] = key;
        } else {
            /* Find child to recurse into. */
            while (i >= 0 && key < x->keys[i]) --i;
            ++i;

            if (x->children[i]->n() == 2 * t_ - 1) {
                split_child(x, i);
                if (key > x->keys[i]) ++i;
            }
            insert_nonfull(x->children[i], key);
        }
    }

    /* ---- remove_from ------------------------------------------------------ */
    bool remove_from(BTreeNode *x, int key)
    {
        int i = find_key(x, key);

        if (i < x->n() && x->keys[i] == key) {
            /* Key found in x. */
            if (x->leaf) {
                /* Case 1: simple deletion from a leaf. */
                x->keys.erase(x->keys.begin() + i);
                return true;
            } else {
                return remove_from_internal(x, i);
            }
        } else {
            /* Key not in x -- recurse into child[i]. */
            if (x->leaf) return false;   /* not in tree */

            bool last = (i == x->n());

            /* Case 3: ensure child[i] has at least t keys before recursing. */
            if (x->children[i]->n() < t_) {
                fill(x, i);
                /* fill() may have shifted i. */
                if (last && i > x->n()) --i;
            }

            return remove_from(x->children[i], key);
        }
    }

    /* ---- find_key -- first index where keys[idx] >= key ------------------ */
    int find_key(BTreeNode *x, int key) const
    {
        int i = 0;
        while (i < x->n() && x->keys[i] < key) ++i;
        return i;
    }

    /* ---- remove_from_internal -- Cases 2a, 2b, 2c ----------------------- */
    bool remove_from_internal(BTreeNode *x, int i)
    {
        int key = x->keys[i];

        if (x->children[i]->n() >= t_) {
            /* Case 2a: left child has >= t keys. Replace with predecessor. */
            int pred      = get_predecessor(x->children[i]);
            x->keys[i]    = pred;
            return remove_from(x->children[i], pred);

        } else if (x->children[i + 1]->n() >= t_) {
            /* Case 2b: right child has >= t keys. Replace with successor. */
            int succ      = get_successor(x->children[i + 1]);
            x->keys[i]    = succ;
            return remove_from(x->children[i + 1], succ);

        } else {
            /* Case 2c: both children have t-1 keys. Merge them. */
            merge(x, i);
            return remove_from(x->children[i], key);
        }
    }

    /* ---- get_predecessor -- largest key in subtree rooted at x ----------- */
    int get_predecessor(BTreeNode *x) const
    {
        while (!x->leaf) x = x->children[x->n()];
        return x->keys[x->n() - 1];
    }

    /* ---- get_successor -- smallest key in subtree rooted at x ------------ */
    int get_successor(BTreeNode *x) const
    {
        while (!x->leaf) x = x->children[0];
        return x->keys[0];
    }

    /* ---- fill -- ensure child[i] has at least t keys (Case 3) ------------ */
    void fill(BTreeNode *x, int i)
    {
        if (i != 0 && x->children[i - 1]->n() >= t_) {
            borrow_from_prev(x, i);
        } else if (i != x->n() && x->children[i + 1]->n() >= t_) {
            borrow_from_next(x, i);
        } else {
            /* Both siblings have t-1 keys -- merge. */
            if (i != x->n()) merge(x, i);
            else              merge(x, i - 1);
        }
    }

    /* ---- borrow_from_prev -- take one key from left sibling -------------- */
    void borrow_from_prev(BTreeNode *x, int i)
    {
        BTreeNode *child  = x->children[i];
        BTreeNode *sibling = x->children[i - 1];

        /* Push x->keys[i-1] down into front of child. */
        child->keys.insert(child->keys.begin(), x->keys[i - 1]);

        /* If internal, move last child of sibling into front of child. */
        if (!child->leaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }

        /* Pull sibling's last key up into x. */
        x->keys[i - 1] = sibling->keys.back();
        sibling->keys.pop_back();
    }

    /* ---- borrow_from_next -- take one key from right sibling ------------- */
    void borrow_from_next(BTreeNode *x, int i)
    {
        BTreeNode *child   = x->children[i];
        BTreeNode *sibling = x->children[i + 1];

        /* Push x->keys[i] down to end of child. */
        child->keys.push_back(x->keys[i]);

        /* If internal, move first child of sibling to end of child. */
        if (!child->leaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }

        /* Pull sibling's first key up into x. */
        x->keys[i] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());
    }

    /* ---- merge -- merge child[i+1] into child[i], pulling key[i] down ---- */
    void merge(BTreeNode *x, int i)
    {
        BTreeNode *child   = x->children[i];
        BTreeNode *sibling = x->children[i + 1];

        /* Pull the separator key down from x into child. */
        child->keys.push_back(x->keys[i]);

        /* Move all keys and children from sibling into child. */
        child->keys.insert(child->keys.end(), sibling->keys.begin(), sibling->keys.end());
        if (!child->leaf) {
            child->children.insert(child->children.end(),
                                   sibling->children.begin(),
                                   sibling->children.end());
            sibling->children.clear();   /* prevent double-free in ~BTreeNode */
        }

        /* Remove separator and sibling pointer from x. */
        x->keys.erase(x->keys.begin() + i);
        x->children.erase(x->children.begin() + i + 1);
        delete sibling;
    }

    /* ---- in_order -------------------------------------------------------- */
    void in_order(BTreeNode *x) const
    {
        for (int i = 0; i < x->n(); ++i) {
            if (!x->leaf) in_order(x->children[i]);
            std::cout << x->keys[i] << " ";
        }
        if (!x->leaf) in_order(x->children[x->n()]);
    }

    /* ---- print_level ----------------------------------------------------- */
    void print_level(BTreeNode *x, int depth) const
    {
        std::string indent(depth * 4, ' ');
        std::cout << indent << "[";
        for (int i = 0; i < x->n(); ++i) {
            if (i) std::cout << ", ";
            std::cout << x->keys[i];
        }
        std::cout << "]\n";
        for (BTreeNode *c : x->children) print_level(c, depth + 1);
    }
};


/* ===========================================================================
 *  main -- demonstration
 * =========================================================================== */

int main()
{
    std::cout << "============================================================\n";
    std::cout << "  B-Tree (minimum degree t = 3)\n";
    std::cout << "============================================================\n\n";
    std::cout << "  t = 3: each node holds 2..5 keys, 3..6 children.\n\n";

    BTree bt(3);

    /* --- Phase 1: Insert --- */
    std::cout << "[PHASE 1] Insert\n";
    std::cout << "------------------------------------------------------------\n";
    int vals[] = {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 15, 25, 35, 28, 40, 50, 45, 22};
    for (int v : vals) {
        bt.insert(v);
        std::cout << "  insert(" << v << ")\n";
    }
    std::cout << "\n";
    bt.display();
    std::cout << "\n";
    bt.display_tree();

    /* --- Phase 2: Search --- */
    std::cout << "\n[PHASE 2] Search\n";
    std::cout << "------------------------------------------------------------\n";
    for (int k : {17, 25, 99}) {
        auto [node, idx] = bt.search(k);
        std::cout << "  search(" << k << ") -> ";
        if (node) std::cout << "found (node key[" << idx << "] = " << node->keys[idx] << ")\n";
        else       std::cout << "not found\n";
    }

    /* --- Phase 3: Remove --- */
    std::cout << "\n[PHASE 3] Remove\n";
    std::cout << "------------------------------------------------------------\n";
    for (int k : {6, 17, 30, 1, 50}) {
        bool ok = bt.remove(k);
        std::cout << "  remove(" << k << ") -> " << (ok ? "removed" : "not found") << "\n";
    }
    std::cout << "\n";
    bt.display();
    std::cout << "\n";
    bt.display_tree();

    /* --- Phase 4: Edge cases --- */
    std::cout << "\n[PHASE 4] Edge cases\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << "  remove(99) (absent) -> " << (bt.remove(99) ? "removed" : "not found") << "\n";
    bt.insert(17);
    std::cout << "  re-insert(17)\n";
    bt.display();

    std::cout << "\n============================================================\n";
    std::cout << "  Done.\n";
    std::cout << "============================================================\n";

    return 0;
}
