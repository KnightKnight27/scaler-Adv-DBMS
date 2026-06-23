// Name: Lavya Tanotra
// Roll No: 24BCS10124
// Lab 4: Red-Black Tree + Full B-Tree in C++
//
// Part 1: Red-Black Tree (in-memory self-balancing BST)
//   Used in: std::map, kernel interval trees, in-memory DB indexes.
//   Invariants guarantee O(log n) height via color + rotation.
//
// Part 2: B-Tree (minimum degree T=2, i.e. 2-3-4 tree)
//   Used in: PostgreSQL/MySQL/SQLite on-disk indexes.
//   Each node holds up to 2T-1 keys → fits in one disk page; minimises I/O.
//
// RBT vs B-Tree:
//   RBT — pointer-chasing, poor cache locality, best in RAM.
//   B-Tree — wide nodes (=1 disk page), short height, excellent for disk.

#include <iostream>
#include <vector>

// ============================================================
// Part 1: Red-Black Tree
// ============================================================

enum Color { RED, BLACK };

struct RBNode {
    int     key;
    Color   color;
    RBNode *left, *right, *parent;
    explicit RBNode(int k)
        : key(k), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
    RBNode* root = nullptr;

    void left_rotate(RBNode* x) {
        RBNode* y = x->right;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if (!x->parent)            root = y;
        else if (x == x->parent->left)  x->parent->left  = y;
        else                             x->parent->right = y;
        y->left = x; x->parent = y;
    }

    void right_rotate(RBNode* x) {
        RBNode* y = x->left;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        y->parent = x->parent;
        if (!x->parent)             root = y;
        else if (x == x->parent->right) x->parent->right = y;
        else                             x->parent->left  = y;
        y->right = x; x->parent = y;
    }

    void fix_insert(RBNode* z) {
        while (z->parent && z->parent->color == RED) {
            RBNode* gp = z->parent->parent;
            if (z->parent == gp->left) {
                RBNode* uncle = gp->right;
                if (uncle && uncle->color == RED) {
                    z->parent->color = uncle->color = BLACK;
                    gp->color = RED; z = gp;
                } else {
                    if (z == z->parent->right) { z = z->parent; left_rotate(z); }
                    z->parent->color = BLACK; gp->color = RED; right_rotate(gp);
                }
            } else {
                RBNode* uncle = gp->left;
                if (uncle && uncle->color == RED) {
                    z->parent->color = uncle->color = BLACK;
                    gp->color = RED; z = gp;
                } else {
                    if (z == z->parent->left) { z = z->parent; right_rotate(z); }
                    z->parent->color = BLACK; gp->color = RED; left_rotate(gp);
                }
            }
        }
        root->color = BLACK;
    }

    void transplant(RBNode* u, RBNode* v) {
        if (!u->parent)           root = v;
        else if (u == u->parent->left) u->parent->left  = v;
        else                           u->parent->right = v;
        if (v) v->parent = u->parent;
    }

    RBNode* minimum(RBNode* n) { while (n->left) n = n->left; return n; }

    void fix_delete(RBNode* x, RBNode* xp) {
        while (x != root && (!x || x->color == BLACK)) {
            if (x == (xp ? xp->left : nullptr)) {
                RBNode* w = xp->right;
                if (w && w->color == RED) {
                    w->color = BLACK; xp->color = RED; left_rotate(xp); w = xp->right;
                }
                if ((!w->left  || w->left->color  == BLACK) &&
                    (!w->right || w->right->color == BLACK)) {
                    if (w) w->color = RED; x = xp; xp = x->parent;
                } else {
                    if (!w->right || w->right->color == BLACK) {
                        if (w->left) w->left->color = BLACK;
                        w->color = RED; right_rotate(w); w = xp->right;
                    }
                    w->color = xp->color; xp->color = BLACK;
                    if (w->right) w->right->color = BLACK;
                    left_rotate(xp); x = root;
                }
            } else {
                RBNode* w = xp->left;
                if (w && w->color == RED) {
                    w->color = BLACK; xp->color = RED; right_rotate(xp); w = xp->left;
                }
                if ((!w->right || w->right->color == BLACK) &&
                    (!w->left  || w->left->color  == BLACK)) {
                    if (w) w->color = RED; x = xp; xp = x->parent;
                } else {
                    if (!w->left || w->left->color == BLACK) {
                        if (w->right) w->right->color = BLACK;
                        w->color = RED; left_rotate(w); w = xp->left;
                    }
                    w->color = xp->color; xp->color = BLACK;
                    if (w->left) w->left->color = BLACK;
                    right_rotate(xp); x = root;
                }
            }
        }
        if (x) x->color = BLACK;
    }

    void inorder(RBNode* n) const {
        if (!n) return;
        inorder(n->left);
        std::cout << n->key << (n->color == RED ? "R" : "B") << " ";
        inorder(n->right);
    }

public:
    void insert(int key) {
        RBNode* z = new RBNode(key);
        RBNode* y = nullptr; RBNode* x = root;
        while (x) { y = x; x = (z->key < x->key) ? x->left : x->right; }
        z->parent = y;
        if (!y)               root = z;
        else if (z->key < y->key) y->left  = z;
        else                       y->right = z;
        fix_insert(z);
    }

    void remove(int key) {
        RBNode* z = root;
        while (z && z->key != key) z = (key < z->key) ? z->left : z->right;
        if (!z) return;
        RBNode* y = z; RBNode* x = nullptr; RBNode* xp = nullptr;
        Color y_orig = y->color;
        if (!z->left)  { x = z->right; xp = z->parent; transplant(z, z->right); }
        else if (!z->right) { x = z->left; xp = z->parent; transplant(z, z->left); }
        else {
            y = minimum(z->right); y_orig = y->color; x = y->right;
            if (y->parent == z) xp = y;
            else {
                xp = y->parent; transplant(y, y->right);
                y->right = z->right; y->right->parent = y;
            }
            transplant(z, y);
            y->left = z->left; y->left->parent = y; y->color = z->color;
        }
        delete z;
        if (y_orig == BLACK) fix_delete(x, xp);
    }

    void print() const { inorder(root); std::cout << "\n"; }
};

// ============================================================
// Part 2: B-Tree (minimum degree T)
// ============================================================

static const int T = 2;   // 2-3-4 tree; increase for larger fanout

struct BNode {
    std::vector<int>    keys;
    std::vector<BNode*> children;
    bool                leaf = true;
    BNode() = default;
};

class BTree {
    BNode* root = nullptr;

    void split_child(BNode* parent, int i) {
        BNode* y = parent->children[i];
        BNode* z = new BNode(); z->leaf = y->leaf;
        int med = y->keys[T - 1];
        z->keys.assign(y->keys.begin() + T, y->keys.end());
        y->keys.resize(T - 1);
        if (!y->leaf) { z->children.assign(y->children.begin() + T, y->children.end()); y->children.resize(T); }
        parent->keys.insert(parent->keys.begin() + i, med);
        parent->children.insert(parent->children.begin() + i + 1, z);
    }

    void insert_nonfull(BNode* n, int key) {
        int i = (int)n->keys.size() - 1;
        if (n->leaf) {
            n->keys.push_back(0);
            while (i >= 0 && key < n->keys[i]) { n->keys[i+1] = n->keys[i]; i--; }
            n->keys[i+1] = key;
        } else {
            while (i >= 0 && key < n->keys[i]) i--;
            i++;
            if ((int)n->children[i]->keys.size() == 2*T-1) {
                split_child(n, i);
                if (key > n->keys[i]) i++;
            }
            insert_nonfull(n->children[i], key);
        }
    }

    int predecessor(BNode* n, int idx) { BNode* c = n->children[idx]; while (!c->leaf) c = c->children.back(); return c->keys.back(); }
    int successor  (BNode* n, int idx) { BNode* c = n->children[idx+1]; while (!c->leaf) c = c->children.front(); return c->keys.front(); }

    void merge(BNode* n, int idx) {
        BNode* L = n->children[idx]; BNode* R = n->children[idx+1];
        L->keys.push_back(n->keys[idx]);
        L->keys.insert(L->keys.end(), R->keys.begin(), R->keys.end());
        if (!L->leaf) L->children.insert(L->children.end(), R->children.begin(), R->children.end());
        n->keys.erase(n->keys.begin() + idx);
        n->children.erase(n->children.begin() + idx + 1);
        delete R;
    }

    void fill(BNode* n, int idx) {
        if (idx > 0 && (int)n->children[idx-1]->keys.size() >= T) {
            BNode* child = n->children[idx]; BNode* sib = n->children[idx-1];
            child->keys.insert(child->keys.begin(), n->keys[idx-1]);
            n->keys[idx-1] = sib->keys.back(); sib->keys.pop_back();
            if (!child->leaf) { child->children.insert(child->children.begin(), sib->children.back()); sib->children.pop_back(); }
        } else if (idx < (int)n->children.size()-1 && (int)n->children[idx+1]->keys.size() >= T) {
            BNode* child = n->children[idx]; BNode* sib = n->children[idx+1];
            child->keys.push_back(n->keys[idx]);
            n->keys[idx] = sib->keys.front(); sib->keys.erase(sib->keys.begin());
            if (!child->leaf) { child->children.push_back(sib->children.front()); sib->children.erase(sib->children.begin()); }
        } else {
            if (idx < (int)n->children.size()-1) merge(n, idx); else merge(n, idx-1);
        }
    }

    void delete_key(BNode* n, int key) {
        int idx = 0;
        while (idx < (int)n->keys.size() && key > n->keys[idx]) idx++;
        if (idx < (int)n->keys.size() && n->keys[idx] == key) {
            if (n->leaf) { n->keys.erase(n->keys.begin() + idx); }
            else if ((int)n->children[idx]->keys.size() >= T) { int p = predecessor(n, idx); n->keys[idx] = p; delete_key(n->children[idx], p); }
            else if ((int)n->children[idx+1]->keys.size() >= T) { int s = successor(n, idx); n->keys[idx] = s; delete_key(n->children[idx+1], s); }
            else { merge(n, idx); delete_key(n->children[idx], key); }
        } else {
            if (n->leaf) { std::cout << "Key not found\n"; return; }
            bool last = (idx == (int)n->children.size());
            if ((int)n->children[last ? idx-1 : idx]->keys.size() < T) fill(n, last ? idx-1 : idx);
            if (last && idx > (int)n->keys.size()) delete_key(n->children[idx-1], key);
            else delete_key(n->children[idx], key);
        }
    }

    void inorder(BNode* n) const {
        if (!n) return;
        for (int i = 0; i < (int)n->keys.size(); i++) {
            if (!n->leaf) inorder(n->children[i]);
            std::cout << n->keys[i] << " ";
        }
        if (!n->leaf) inorder(n->children.back());
    }

    bool search(BNode* n, int key) const {
        int i = 0;
        while (i < (int)n->keys.size() && key > n->keys[i]) i++;
        if (i < (int)n->keys.size() && n->keys[i] == key) return true;
        if (n->leaf) return false;
        return search(n->children[i], key);
    }

public:
    void insert(int key) {
        if (!root) { root = new BNode(); root->keys.push_back(key); return; }
        if ((int)root->keys.size() == 2*T-1) {
            BNode* s = new BNode(); s->leaf = false; s->children.push_back(root);
            split_child(s, 0); root = s;
        }
        insert_nonfull(root, key);
    }

    void remove(int key) {
        if (!root) return;
        delete_key(root, key);
        if (root->keys.empty() && !root->leaf) { BNode* old = root; root = root->children[0]; delete old; }
    }

    bool search(int key) const { return root && search(root, key); }
    void print() const { if (root) inorder(root); std::cout << "\n"; }
};

// ============================================================
// main
// ============================================================

int main() {
    std::cout << "=== Part 1: Red-Black Tree ===\n";
    RedBlackTree rbt;
    for (int k : {10, 20, 30, 15, 25, 5, 1}) rbt.insert(k);
    std::cout << "Inorder (key + R/B):\n"; rbt.print();
    rbt.remove(20);
    std::cout << "After remove(20):\n"; rbt.print();

    std::cout << "\n=== Part 2: B-Tree (T=2) ===\n";
    BTree bt;
    for (int k : {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25}) bt.insert(k);
    std::cout << "Inorder after inserts:\n"; bt.print();
    std::cout << "search(17): " << (bt.search(17) ? "found" : "not found") << "\n";
    std::cout << "search(99): " << (bt.search(99) ? "found" : "not found") << "\n";
    bt.remove(6); bt.remove(20);
    std::cout << "After remove(6), remove(20):\n"; bt.print();

    return 0;
}

// Compile: g++ -std=c++17 -o rbt_btree rbt_btree.cpp
// Run:     ./rbt_btree
