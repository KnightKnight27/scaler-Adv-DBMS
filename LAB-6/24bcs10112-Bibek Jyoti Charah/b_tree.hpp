// Lab 6 - B-Tree (minimum degree t, full insert / search / delete)
// Bibek Jyoti Charah (24bcs10112)
//
// Each node keeps its key/value pairs in one sorted `entries` vector and,
// when internal, one more child pointer than entries. A node holds between
// t-1 and 2t-1 entries (the root may hold fewer). Insert splits full nodes
// on the way down; erase tops a thin child back up to t entries (borrow or
// merge) before descending, so a single downward pass always suffices.

#pragma once

#include <iostream>
#include <string>
#include <utility>
#include <vector>

template <typename Key, typename Value>
class BTree {
    struct Entry { Key key; Value value; };

    struct Node {
        bool leaf;
        std::vector<Entry> entries;
        std::vector<Node *> children;

        explicit Node(bool is_leaf) : leaf(is_leaf) {}
        ~Node() { for (Node *c : children) delete c; }

        int n() const { return static_cast<int>(entries.size()); }
        bool full(int t) const { return n() == 2 * t - 1; }
    };

public:
    explicit BTree(int degree = 3) : t(degree < 2 ? 2 : degree) {}
    ~BTree() { delete root; }

    BTree(const BTree &) = delete;
    BTree &operator=(const BTree &) = delete;

    std::size_t size() const { return count; }
    bool empty() const { return count == 0; }

    // Insert or overwrite.
    void put(const Key &key, const Value &value) {
        if (!root) {
            root = new Node(true);
            root->entries.push_back({key, value});
            ++count;
            return;
        }
        if (root->full(t)) {
            Node *fresh = new Node(false);
            fresh->children.push_back(root);
            root = fresh;
            splitChild(root, 0);
        }
        if (insertNonFull(root, key, value)) ++count;
    }

    const Value *get(const Key &key) const {
        for (Node *x = root; x;) {
            int i = lowerBound(x, key);
            if (i < x->n() && x->entries[i].key == key) return &x->entries[i].value;
            if (x->leaf) return nullptr;
            x = x->children[i];
        }
        return nullptr;
    }

    bool contains(const Key &key) const { return get(key) != nullptr; }

    bool erase(const Key &key) {
        if (!root || !eraseFrom(root, key)) return false;
        if (root->entries.empty()) {            // root shrank away
            Node *old = root;
            root = root->leaf ? nullptr : root->children.front();
            if (old) old->children.clear();
            delete old;
        }
        --count;
        return true;
    }

    template <typename Fn>
    void for_each(Fn fn) const { if (root) walk(root, fn); }

    void print(std::ostream &os = std::cout) const {
        if (root) show(root, 0, os);
        else os << "<empty>\n";
    }

    // Structural sanity check: fanout limits, key ordering, equal leaf depth.
    bool check() const {
        if (!root) return true;
        int leaf_depth = -1;
        return verify(root, true, 0, leaf_depth);
    }

private:
    Node *root = nullptr;
    int t;
    std::size_t count = 0;

    int lowerBound(const Node *x, const Key &key) const {
        int i = 0;
        while (i < x->n() && x->entries[i].key < key) ++i;
        return i;
    }

    // Split child[i] of `parent` (which must be full) about its median.
    void splitChild(Node *parent, int i) {
        Node *left = parent->children[i];
        Node *right = new Node(left->leaf);

        for (int j = t; j < left->n(); ++j)
            right->entries.push_back(std::move(left->entries[j]));
        if (!left->leaf) {
            for (int j = t; j < static_cast<int>(left->children.size()); ++j)
                right->children.push_back(left->children[j]);
            left->children.resize(t);
        }

        Entry median = std::move(left->entries[t - 1]);
        left->entries.resize(t - 1);

        parent->children.insert(parent->children.begin() + i + 1, right);
        parent->entries.insert(parent->entries.begin() + i, std::move(median));
    }

    // Returns true if a new key was added, false on overwrite.
    bool insertNonFull(Node *x, const Key &key, const Value &value) {
        int i = lowerBound(x, key);
        if (i < x->n() && x->entries[i].key == key) {
            x->entries[i].value = value;
            return false;
        }
        if (x->leaf) {
            x->entries.insert(x->entries.begin() + i, {key, value});
            return true;
        }
        if (x->children[i]->full(t)) {
            splitChild(x, i);
            if (x->entries[i].key == key) { x->entries[i].value = value; return false; }
            if (x->entries[i].key < key) ++i;
        }
        return insertNonFull(x->children[i], key, value);
    }

    Entry popMin(Node *x) {
        while (!x->leaf) x = x->children[fixChild(x, 0)];
        Entry e = std::move(x->entries.front());
        x->entries.erase(x->entries.begin());
        return e;
    }

    Entry popMax(Node *x) {
        while (!x->leaf) x = x->children[fixChild(x, x->n())];
        Entry e = std::move(x->entries.back());
        x->entries.pop_back();
        return e;
    }

    void merge(Node *parent, int i) {
        Node *left = parent->children[i];
        Node *right = parent->children[i + 1];

        left->entries.push_back(std::move(parent->entries[i]));
        for (auto &e : right->entries) left->entries.push_back(std::move(e));
        for (Node *c : right->children) left->children.push_back(c);
        right->children.clear();

        parent->entries.erase(parent->entries.begin() + i);
        parent->children.erase(parent->children.begin() + i + 1);
        delete right;
    }

    // Ensure child[i] has at least t entries before we descend into it, by
    // borrowing from a sibling or merging. Returns the (possibly shifted)
    // index of the child to descend into.
    int fixChild(Node *parent, int i) {
        Node *child = parent->children[i];
        if (child->n() >= t) return i;

        Node *left = i > 0 ? parent->children[i - 1] : nullptr;
        Node *right = i < parent->n() ? parent->children[i + 1] : nullptr;

        if (left && left->n() >= t) {                       // borrow from left
            child->entries.insert(child->entries.begin(), std::move(parent->entries[i - 1]));
            parent->entries[i - 1] = std::move(left->entries.back());
            left->entries.pop_back();
            if (!child->leaf) {
                child->children.insert(child->children.begin(), left->children.back());
                left->children.pop_back();
            }
            return i;
        }
        if (right && right->n() >= t) {                     // borrow from right
            child->entries.push_back(std::move(parent->entries[i]));
            parent->entries[i] = std::move(right->entries.front());
            right->entries.erase(right->entries.begin());
            if (!child->leaf) {
                child->children.push_back(right->children.front());
                right->children.erase(right->children.begin());
            }
            return i;
        }
        if (right) { merge(parent, i); return i; }          // merge with right
        merge(parent, i - 1);                               // or with left
        return i - 1;
    }

    bool eraseFrom(Node *x, const Key &key) {
        int i = lowerBound(x, key);
        bool here = i < x->n() && x->entries[i].key == key;

        if (here && x->leaf) {
            x->entries.erase(x->entries.begin() + i);
            return true;
        }
        if (here) {                                         // internal node
            Node *left = x->children[i];
            Node *right = x->children[i + 1];
            if (left->n() >= t)  { x->entries[i] = popMax(left);  return true; }
            if (right->n() >= t) { x->entries[i] = popMin(right); return true; }
            merge(x, i);
            return eraseFrom(left, key);
        }
        if (x->leaf) return false;
        return eraseFrom(x->children[fixChild(x, i)], key);
    }

    template <typename Fn>
    void walk(const Node *x, Fn &fn) const {
        for (int i = 0; i < x->n(); ++i) {
            if (!x->leaf) walk(x->children[i], fn);
            fn(x->entries[i].key, x->entries[i].value);
        }
        if (!x->leaf) walk(x->children[x->n()], fn);
    }

    static void show(const Node *x, int depth, std::ostream &os) {
        os << std::string(static_cast<std::size_t>(depth) * 2, ' ') << '[';
        for (int i = 0; i < x->n(); ++i) {
            if (i) os << ' ';
            os << x->entries[i].key;
        }
        os << ']' << (x->leaf ? " *\n" : "\n");
        for (const Node *c : x->children) show(c, depth + 1, os);
    }

    bool verify(const Node *x, bool is_root, int depth, int &leaf_depth) const {
        int n = x->n();
        if (n > 2 * t - 1) return false;
        if (!is_root && n < t - 1) return false;
        for (int i = 1; i < n; ++i)
            if (!(x->entries[i - 1].key < x->entries[i].key)) return false;

        if (x->leaf) {
            if (leaf_depth == -1) leaf_depth = depth;
            return leaf_depth == depth && x->children.empty();
        }
        if (static_cast<int>(x->children.size()) != n + 1) return false;
        for (int i = 0; i <= n; ++i)
            if (!verify(x->children[i], false, depth + 1, leaf_depth)) return false;
        return true;
    }
};
