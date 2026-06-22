/*
 * =============================================================================
 *  B-Tree Index (CLRS Chapter 18)
 * =============================================================================
 *
 *  Course  : Advanced DBMS (Scaler)
 *  Author  : Praveen Kumar
 *  Date    : 2026-05-30
 *
 *  Purpose : Implement a B-tree index storing key-value pairs (int key,
 *            string value).  Every internal node holds between t-1 and 2t-1
 *            keys.  Supports:
 *              - insert   (preemptive split on the way down)
 *              - search   (returns value for a given key)
 *              - remove   (handles all 3 CLRS cases)
 *              - in-order display (keys + values in sorted order)
 *              - tree structure display (level-order)
 *
 *  In a DBMS this is the structure underlying InnoDB's clustered index,
 *  SQLite's table B-tree, and PostgreSQL's B-tree access method.  The
 *  minimum degree t determines the page fill factor: with t=512 and 8 KB
 *  pages each node holds up to 1023 key-value pairs and a typical database
 *  is 3-4 levels deep regardless of table size.
 *
 *  Build   : g++ -std=c++17 -O2 -Wall -Wextra -o btree btree.cpp
 *  Run     : ./btree
 * =============================================================================
 */

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <iomanip>
#include <algorithm>

/* ===========================================================================
 *  Entry -- one key-value pair stored in a node
 * =========================================================================== */

struct Entry {
    int         key;
    std::string value;
};


/* ===========================================================================
 *  BTreeNode
 * ===========================================================================
 *
 *  Each node stores:
 *    entries  : sorted vector of Entry (key-value pairs), up to 2t-1
 *    children : child pointers (entries.size() + 1 for internal nodes)
 *    leaf     : true if this is a leaf node
 *
 *  B-tree invariants (CLRS 18.1):
 *    1. Each node x stores x.n entries in non-decreasing key order.
 *    2. If x is internal it has x.n + 1 children.
 *    3. Keys in child subtree i satisfy: entry[i-1].key < child[i].keys < entry[i].key
 *    4. All leaves have the same depth.
 *    5. t-1 <= x.n <= 2t-1  (root: 1 <= root.n <= 2t-1)
 * -------------------------------------------------------------------------- */

struct BTreeNode {
    std::vector<Entry>      entries;
    std::vector<BTreeNode*> children;
    bool                    leaf;

    explicit BTreeNode(bool is_leaf) : leaf(is_leaf) {}

    ~BTreeNode()
    {
        for (BTreeNode *c : children) delete c;
    }

    int n() const { return static_cast<int>(entries.size()); }
};


/* ===========================================================================
 *  BTree -- B-tree index with minimum degree t
 * =========================================================================== */

class BTree {
public:
    explicit BTree(int t) : t_(t), root_(new BTreeNode(true)) {}
    ~BTree() { delete root_; }

    /* -----------------------------------------------------------------------
     *  search -- return pointer to Entry if found, nullptr otherwise.
     *
     *  Complexity: O(t * log_t(n)) descending from the root.
     * ----------------------------------------------------------------------- */
    const Entry* search(int key) const
    {
        return search_node(root_, key);
    }

    bool contains(int key) const { return search(key) != nullptr; }

    /* -----------------------------------------------------------------------
     *  insert -- preemptive split variant (CLRS 18.3).
     *
     *  Split full nodes on the way DOWN so we never need to walk back up.
     *  A node is "full" when it has 2t-1 entries.
     *
     *  If the key already exists the value is updated in place.
     * ----------------------------------------------------------------------- */
    void insert(int key, const std::string &value)
    {
        BTreeNode *r = root_;

        if (r->n() == 2 * t_ - 1) {
            /* Root is full -- split it.  Tree grows one level. */
            BTreeNode *s = new BTreeNode(false);
            root_        = s;
            s->children.push_back(r);
            split_child(s, 0);
            insert_nonfull(s, key, value);
        } else {
            insert_nonfull(r, key, value);
        }
    }

    /* -----------------------------------------------------------------------
     *  remove -- CLRS 18.3, three cases.
     *
     *  Case 1: key is in a leaf -- delete directly.
     *  Case 2: key is in an internal node:
     *    2a: left child has >= t entries -- replace with predecessor.
     *    2b: right child has >= t entries -- replace with successor.
     *    2c: both have t-1 -- merge, recurse into merged child.
     *  Case 3: key not in current node:
     *    3a/3b: ensure child has >= t entries (borrow or merge) before recursing.
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
     *  display -- in-order traversal (ascending key order, with values)
     * ----------------------------------------------------------------------- */
    void display() const
    {
        std::cout << "  In-order:\n";
        in_order(root_);
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

    /* ---- find_pos: first index where entries[i].key >= key --------------- */
    int find_pos(BTreeNode *x, int key) const
    {
        int i = 0;
        while (i < x->n() && x->entries[i].key < key) ++i;
        return i;
    }

    /* ---- search_node ----------------------------------------------------- */
    const Entry* search_node(BTreeNode *x, int key) const
    {
        int i = find_pos(x, key);
        if (i < x->n() && x->entries[i].key == key)
            return &x->entries[i];
        if (x->leaf)
            return nullptr;
        return search_node(x->children[i], key);
    }

    /* ---- split_child -----------------------------------------------------
     *
     *  Split x->children[i] (full: 2t-1 entries) into two nodes of t-1
     *  entries each.  The median entry is promoted into x.
     *
     *  Before:            After:
     *    x: [... ki ...]    x: [... ki  MED  ki+1 ...]
     *            |                    |       |
     *        y (full)             y (L)     z (R)
     * ---------------------------------------------------------------------- */
    void split_child(BTreeNode *x, int i)
    {
        BTreeNode *y = x->children[i];   /* full child */
        BTreeNode *z = new BTreeNode(y->leaf);

        /* Right half of y's entries go to z. */
        z->entries.assign(y->entries.begin() + t_, y->entries.end());
        y->entries.resize(t_ - 1);

        /* If internal, right half of children go to z. */
        if (!y->leaf) {
            z->children.assign(y->children.begin() + t_, y->children.end());
            y->children.resize(t_);
        }

        /* Promote median entry into x. */
        Entry median = y->entries[t_ - 1];
        y->entries.resize(t_ - 1);

        x->children.insert(x->children.begin() + i + 1, z);
        x->entries.insert(x->entries.begin() + i, median);
    }

    /* ---- insert_nonfull -------------------------------------------------- */
    void insert_nonfull(BTreeNode *x, int key, const std::string &value)
    {
        int i = x->n() - 1;

        if (x->leaf) {
            /* Check for key update first. */
            for (auto &e : x->entries) {
                if (e.key == key) { e.value = value; return; }
            }
            /* Shift entries right and insert. */
            x->entries.push_back({0, ""});
            while (i >= 0 && key < x->entries[i].key) {
                x->entries[i + 1] = x->entries[i];
                --i;
            }
            x->entries[i + 1] = {key, value};
        } else {
            /* Find child to recurse into. */
            while (i >= 0 && key < x->entries[i].key) --i;

            /* Check if key matches an entry in this node. */
            if (i >= 0 && x->entries[i].key == key) {
                x->entries[i].value = value;
                return;
            }

            ++i;
            if (x->children[i]->n() == 2 * t_ - 1) {
                split_child(x, i);
                if (key > x->entries[i].key) ++i;
                else if (key == x->entries[i].key) {
                    x->entries[i].value = value;
                    return;
                }
            }
            insert_nonfull(x->children[i], key, value);
        }
    }

    /* ---- remove_from ----------------------------------------------------- */
    bool remove_from(BTreeNode *x, int key)
    {
        int i = find_pos(x, key);

        if (i < x->n() && x->entries[i].key == key) {
            if (x->leaf) {
                /* Case 1: delete from leaf. */
                x->entries.erase(x->entries.begin() + i);
                return true;
            } else {
                return remove_from_internal(x, i);
            }
        } else {
            if (x->leaf) return false;

            bool last = (i == x->n());
            if (x->children[i]->n() < t_) {
                fill(x, i);
                if (last && i > x->n()) --i;
            }
            return remove_from(x->children[i], key);
        }
    }

    /* ---- remove_from_internal -- Cases 2a, 2b, 2c ----------------------- */
    bool remove_from_internal(BTreeNode *x, int i)
    {
        int key = x->entries[i].key;

        if (x->children[i]->n() >= t_) {
            /* Case 2a: predecessor */
            Entry pred      = get_predecessor(x->children[i]);
            x->entries[i]   = pred;
            return remove_from(x->children[i], pred.key);

        } else if (x->children[i + 1]->n() >= t_) {
            /* Case 2b: successor */
            Entry succ      = get_successor(x->children[i + 1]);
            x->entries[i]   = succ;
            return remove_from(x->children[i + 1], succ.key);

        } else {
            /* Case 2c: merge */
            merge(x, i);
            return remove_from(x->children[i], key);
        }
    }

    Entry get_predecessor(BTreeNode *x) const
    {
        while (!x->leaf) x = x->children[x->n()];
        return x->entries[x->n() - 1];
    }

    Entry get_successor(BTreeNode *x) const
    {
        while (!x->leaf) x = x->children[0];
        return x->entries[0];
    }

    /* ---- fill -- ensure child[i] has >= t entries (Case 3) --------------- */
    void fill(BTreeNode *x, int i)
    {
        if (i != 0 && x->children[i - 1]->n() >= t_)
            borrow_from_prev(x, i);
        else if (i != x->n() && x->children[i + 1]->n() >= t_)
            borrow_from_next(x, i);
        else {
            if (i != x->n()) merge(x, i);
            else              merge(x, i - 1);
        }
    }

    void borrow_from_prev(BTreeNode *x, int i)
    {
        BTreeNode *child   = x->children[i];
        BTreeNode *sibling = x->children[i - 1];

        child->entries.insert(child->entries.begin(), x->entries[i - 1]);
        if (!child->leaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }
        x->entries[i - 1] = sibling->entries.back();
        sibling->entries.pop_back();
    }

    void borrow_from_next(BTreeNode *x, int i)
    {
        BTreeNode *child   = x->children[i];
        BTreeNode *sibling = x->children[i + 1];

        child->entries.push_back(x->entries[i]);
        if (!child->leaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }
        x->entries[i] = sibling->entries.front();
        sibling->entries.erase(sibling->entries.begin());
    }

    void merge(BTreeNode *x, int i)
    {
        BTreeNode *child   = x->children[i];
        BTreeNode *sibling = x->children[i + 1];

        child->entries.push_back(x->entries[i]);
        child->entries.insert(child->entries.end(),
                              sibling->entries.begin(),
                              sibling->entries.end());
        if (!child->leaf) {
            child->children.insert(child->children.end(),
                                   sibling->children.begin(),
                                   sibling->children.end());
            sibling->children.clear();
        }
        x->entries.erase(x->entries.begin() + i);
        x->children.erase(x->children.begin() + i + 1);
        delete sibling;
    }

    /* ---- traversal helpers ----------------------------------------------- */

    void in_order(BTreeNode *x) const
    {
        for (int i = 0; i < x->n(); ++i) {
            if (!x->leaf) in_order(x->children[i]);
            std::cout << "    " << std::setw(4) << x->entries[i].key
                      << " -> " << x->entries[i].value << "\n";
        }
        if (!x->leaf) in_order(x->children[x->n()]);
    }

    void print_level(BTreeNode *x, int depth) const
    {
        std::string indent(depth * 4, ' ');
        std::cout << indent << "[";
        for (int i = 0; i < x->n(); ++i) {
            if (i) std::cout << ", ";
            std::cout << x->entries[i].key << ":\"" << x->entries[i].value << "\"";
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
    std::cout << "  B-Tree Index (minimum degree t = 3)\n";
    std::cout << "============================================================\n\n";
    std::cout << "  t = 3: each node holds 2..5 key-value pairs,\n";
    std::cout << "         internal nodes have 3..6 children.\n\n";

    BTree bt(3);

    /* --- Task 1 & 2: B-Tree initialization and record insertion --- */
    std::cout << "[TASK 1 & 2] Initialize tree and insert key-value pairs\n";
    std::cout << "------------------------------------------------------------\n";

    struct KV { int key; const char *value; };
    KV records[] = {
        {10, "Database Internals"},
        {20, "DDIA"},
        {5,  "OSTEP"},
        {6,  "CLRS"},
        {12, "Clean Code"},
        {30, "TCP/IP Illustrated"},
        {7,  "Linux Programming Interface"},
        {17, "Compilers (Dragon Book)"},
        {3,  "SICP"},
        {1,  "The C Programming Language"},
        {15, "Modern Operating Systems"},
        {25, "Pragmatic Programmer"},
        {35, "CS:APP"},
        {28, "UNIX Network Programming"},
        {40, "Computer Networks"},
        {50, "Mythical Man-Month"},
        {45, "Refactoring"},
        {22, "Site Reliability Engineering"},
    };

    for (auto &r : records) {
        bt.insert(r.key, r.value);
        std::cout << "  insert(" << r.key << ", \"" << r.value << "\")\n";
    }

    /* --- Task 5: Tree structure analysis --- */
    std::cout << "\n[TASK 5] Tree structure after insertions\n";
    std::cout << "------------------------------------------------------------\n";
    bt.display_tree();

    /* --- In-order (sorted) display --- */
    std::cout << "\n[TASK 6] In-order display (indexed, sorted by key)\n";
    std::cout << "------------------------------------------------------------\n";
    bt.display();

    /* --- Task 3: Node splitting observation ---
     * Splits happen automatically during insert above.
     * At t=3 a node splits when it reaches 5 entries.
     * The first split occurs when inserting key 12 (6th key).
     * Median is promoted to the root; two child nodes of 2 entries each remain.
     * Subsequent splits propagate upward similarly. */
    std::cout << "\n[TASK 3] Splitting note\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << "  t=3: nodes split when they reach 2t-1 = 5 entries.\n";
    std::cout << "  The median entry is promoted to the parent.\n";
    std::cout << "  Left child keeps t-1=2 entries; right child gets t-1=2 entries.\n";
    std::cout << "  Root splits when full, growing the tree height by one.\n\n";

    /* --- Task 4: Search operations --- */
    std::cout << "[TASK 4] Search operations\n";
    std::cout << "------------------------------------------------------------\n";
    for (int k : {17, 25, 1, 99}) {
        const Entry *e = bt.search(k);
        std::cout << "  search(" << k << ") -> ";
        if (e) std::cout << "found: \"" << e->value << "\"\n";
        else    std::cout << "not found\n";
    }

    /* --- Remove --- */
    std::cout << "\n[REMOVE] Deletion test\n";
    std::cout << "------------------------------------------------------------\n";
    for (int k : {6, 17, 30, 1, 50}) {
        bool ok = bt.remove(k);
        std::cout << "  remove(" << k << ") -> " << (ok ? "removed" : "not found") << "\n";
    }

    std::cout << "\n  Tree after deletions:\n";
    bt.display_tree();

    std::cout << "\n  In-order after deletions:\n";
    bt.display();

    /* --- Edge case: update existing key --- */
    std::cout << "\n[EDGE CASE] Update value for existing key\n";
    std::cout << "------------------------------------------------------------\n";
    bt.insert(25, "Pragmatic Programmer (2nd ed.)");
    const Entry *e = bt.search(25);
    std::cout << "  search(25) -> \"" << (e ? e->value : "not found") << "\"\n";

    std::cout << "\n============================================================\n";
    std::cout << "  Done.\n";
    std::cout << "============================================================\n";

    return 0;
}
