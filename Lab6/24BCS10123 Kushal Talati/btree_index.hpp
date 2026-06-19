// Lab 6 — B-Tree (header-only, generic ordered map keyed by branching factor)
// 24BCS10123  Kushal Talati
//
// kt::BTreeIndex<Key, Value, Compare> implements the standard CLRS chapter-18
// B-tree: proactive-split on the way down for insert, predecessor/successor/
// merge cases for erase, sorted iteration, and a runtime invariant checker
// that walks every node.
//
// Implementation choice that differs from the typical textbook layout:
// each node stores an array of *paired* {key, value} entries (Entry struct)
// rather than two parallel std::vector<Key> / std::vector<Value>. Array-of-
// pairs keeps a key adjacent to its value in memory, which is what you would
// want if this were actually paged to disk (the value moves with the key
// during a split, with no risk of an index drifting between two vectors).
//
// The minimum-branching parameter is `min_branch` (the CLRS `t`). Every
// non-root node carries between `min_branch - 1` and `2*min_branch - 1`
// entries; the root may carry fewer when the tree is small.

#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kt {

template <typename Key, typename Value, typename Compare = std::less<Key>>
class BTreeIndex {
public:
    explicit BTreeIndex(int min_branch = 3)
        : t_(min_branch < 2 ? 2 : min_branch) {}

    BTreeIndex(const BTreeIndex&)            = delete;
    BTreeIndex& operator=(const BTreeIndex&) = delete;
    BTreeIndex(BTreeIndex&&)                 = delete;
    BTreeIndex& operator=(BTreeIndex&&)      = delete;

    ~BTreeIndex() { delete head_; }

    // --- modifiers --------------------------------------------------------

    // Insert or overwrite. Returns true if a new entry was created.
    bool put(const Key& k, const Value& v) {
        if (!head_) {
            head_ = new Node(/*is_leaf=*/true);
            head_->cells.push_back({k, v});
            ++count_;
            return true;
        }
        if (head_->is_full(t_)) {
            // The only operation that grows the tree's height.
            Node* fresh = new Node(/*is_leaf=*/false);
            fresh->kids.push_back(head_);
            split_child_at(fresh, 0);
            head_ = fresh;
        }
        return insert_into_nonfull(head_, k, v);
    }

    // Returns true if `k` was present and was removed.
    bool take(const Key& k) {
        if (!head_) return false;
        bool removed = erase_below(head_, k);
        // Root may be left empty after a merge — drop a level if so.
        if (head_->cells.empty() && !head_->leaf) {
            Node* old = head_;
            head_     = head_->kids.front();
            old->kids.clear();
            delete old;
        } else if (head_->cells.empty() && head_->leaf) {
            delete head_;
            head_ = nullptr;
        }
        if (removed) --count_;
        return removed;
    }

    // --- queries ----------------------------------------------------------

    bool has(const Key& k) const {
        auto [n, _] = find_in_tree(k);
        return n != nullptr;
    }

    std::optional<Value> get(const Key& k) const {
        auto [n, i] = find_in_tree(k);
        if (!n) return std::nullopt;
        return n->cells[static_cast<std::size_t>(i)].value;
    }

    Value& fetch(const Key& k) {
        auto [n, i] = find_in_tree(k);
        if (!n) throw std::out_of_range("BTreeIndex::fetch — missing key");
        return n->cells[static_cast<std::size_t>(i)].value;
    }

    std::size_t length() const noexcept { return count_; }
    bool        empty()  const noexcept { return count_ == 0; }
    int         branching() const noexcept { return t_; }

    // Sorted (key, value) traversal. Recursion depth is the tree height,
    // which is O(log_t n) — bounded by ~30 for any realistic dataset.
    template <typename Visitor>
    void scan(Visitor&& visit) const {
        if (head_) scan_rec(head_, visit);
    }

    // Indented level-by-level dump. Useful for the smaller demo cases.
    void dump(std::ostream& os) const {
        if (!head_) { os << "<empty>\n"; return; }
        dump_rec(head_, 0, os);
    }

    // Returns std::nullopt when every B-tree invariant holds; otherwise the
    // first violation discovered. Safe to call after every mutation.
    std::optional<std::string> check() const {
        if (!head_) return std::nullopt;
        int leaf_depth = -1;
        std::string err = check_rec(head_, /*is_root=*/true,
                                    /*depth=*/0, leaf_depth);
        if (err.empty()) return std::nullopt;
        return err;
    }

private:
    struct Entry {
        Key   key{};
        Value value{};
    };

    struct Node {
        std::vector<Entry> cells;
        std::vector<Node*> kids;
        bool               leaf;
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
        ~Node() { for (Node* c : kids) delete c; }

        bool is_full(int t) const { return static_cast<int>(cells.size()) == 2*t - 1; }
        int  ncells()       const { return static_cast<int>(cells.size()); }
    };

    Node*       head_  = nullptr;
    int         t_;
    std::size_t count_ = 0;
    Compare     less_{};

    // Smallest index i with cells[i].key >= k.
    int lower_slot(const Node* n, const Key& k) const {
        int i = 0;
        const int K = n->ncells();
        while (i < K && less_(n->cells[static_cast<std::size_t>(i)].key, k)) ++i;
        return i;
    }

    // --- search ----------------------------------------------------------

    std::pair<Node*, int> find_in_tree(const Key& k) {
        Node* cur = head_;
        while (cur) {
            int i = lower_slot(cur, k);
            if (i < cur->ncells() && !less_(k, cur->cells[static_cast<std::size_t>(i)].key))
                return {cur, i};
            if (cur->leaf) return {nullptr, 0};
            cur = cur->kids[static_cast<std::size_t>(i)];
        }
        return {nullptr, 0};
    }
    std::pair<const Node*, int> find_in_tree(const Key& k) const {
        const Node* cur = head_;
        while (cur) {
            int i = lower_slot(cur, k);
            if (i < cur->ncells() && !less_(k, cur->cells[static_cast<std::size_t>(i)].key))
                return {cur, i};
            if (cur->leaf) return {nullptr, 0};
            cur = cur->kids[static_cast<std::size_t>(i)];
        }
        return {nullptr, 0};
    }

    // --- split + insert --------------------------------------------------

    // Split parent->kids[at] which must currently hold 2t-1 cells.
    void split_child_at(Node* parent, int at) {
        Node* y = parent->kids[static_cast<std::size_t>(at)];
        Node* z = new Node(y->leaf);

        // Move the upper t-1 entries into z.
        z->cells.assign(y->cells.begin() + t_, y->cells.end());
        if (!y->leaf) {
            z->kids.assign(y->kids.begin() + t_, y->kids.end());
            y->kids.erase(y->kids.begin() + t_, y->kids.end());
        }
        // Median (slot t-1) gets promoted into the parent.
        Entry median = std::move(y->cells[static_cast<std::size_t>(t_ - 1)]);
        y->cells.erase(y->cells.begin() + (t_ - 1), y->cells.end());

        parent->kids.insert(parent->kids.begin() + at + 1, z);
        parent->cells.insert(parent->cells.begin() + at, std::move(median));
    }

    bool insert_into_nonfull(Node* n, const Key& k, const Value& v) {
        if (n->leaf) {
            int slot = lower_slot(n, k);
            if (slot < n->ncells() && !less_(k, n->cells[static_cast<std::size_t>(slot)].key)) {
                n->cells[static_cast<std::size_t>(slot)].value = v;   // overwrite
                return false;
            }
            n->cells.insert(n->cells.begin() + slot, Entry{k, v});
            ++count_;
            return true;
        }

        // Internal: scan from the right to find the first cell whose key
        // is <= k; the child to its right is the one we descend into.
        int i = n->ncells() - 1;
        while (i >= 0 && less_(k, n->cells[static_cast<std::size_t>(i)].key)) --i;
        if (i >= 0 && !less_(n->cells[static_cast<std::size_t>(i)].key, k)) {
            n->cells[static_cast<std::size_t>(i)].value = v;          // overwrite hit
            return false;
        }
        ++i;
        if (n->kids[static_cast<std::size_t>(i)]->is_full(t_)) {
            split_child_at(n, i);
            // Median may have landed at slot i; figure out which side of it
            // the new key belongs in.
            const Key& promoted = n->cells[static_cast<std::size_t>(i)].key;
            if (less_(promoted, k))           ++i;
            else if (!less_(k, promoted))     { n->cells[static_cast<std::size_t>(i)].value = v; return false; }
        }
        return insert_into_nonfull(n->kids[static_cast<std::size_t>(i)], k, v);
    }

    // --- erase (CLRS chapter 18) -----------------------------------------

    // Remove k from the subtree rooted at n. Caller guarantees n has >= t
    // cells unless n is the root.
    bool erase_below(Node* n, const Key& k) {
        int i = lower_slot(n, k);
        bool here = (i < n->ncells()) &&
                    !less_(k, n->cells[static_cast<std::size_t>(i)].key);

        if (here && n->leaf) {                                  // case 1 — leaf hit
            n->cells.erase(n->cells.begin() + i);
            return true;
        }
        if (here) return erase_internal_hit(n, i);              // case 2

        if (n->leaf) return false;                              // not present

        // case 3 — descend; ensure target child has >= t cells first.
        bool descending_into_last = (i == n->ncells());
        if (n->kids[static_cast<std::size_t>(i)]->ncells() < t_) {
            ensure_branch_safe(n, i);
        }
        if (descending_into_last && i > n->ncells()) --i;
        return erase_below(n->kids[static_cast<std::size_t>(i)], k);
    }

    bool erase_internal_hit(Node* n, int i) {
        Node* left  = n->kids[static_cast<std::size_t>(i)];
        Node* right = n->kids[static_cast<std::size_t>(i + 1)];

        if (left->ncells() >= t_) {                             // case 2a — predecessor
            Entry pred = pop_max(left);
            n->cells[static_cast<std::size_t>(i)] = std::move(pred);
            return true;
        }
        if (right->ncells() >= t_) {                            // case 2b — successor
            Entry succ = pop_min(right);
            n->cells[static_cast<std::size_t>(i)] = std::move(succ);
            return true;
        }
        // case 2c — both minimal; merge and recurse. After merge, the original
        // separator lives at slot t-1 of `left`. We capture its key BEFORE
        // recursing because the merged node will shift indices around.
        merge_kids(n, i);
        Key target = left->cells[static_cast<std::size_t>(t_ - 1)].key;
        return erase_below(left, target);
    }

    Entry pop_max(Node* n) {
        if (n->leaf) {
            Entry out = std::move(n->cells.back());
            n->cells.pop_back();
            return out;
        }
        int last = n->ncells();
        if (n->kids[static_cast<std::size_t>(last)]->ncells() < t_) ensure_branch_safe(n, last);
        return pop_max(n->kids[static_cast<std::size_t>(n->ncells())]);
    }
    Entry pop_min(Node* n) {
        if (n->leaf) {
            Entry out = std::move(n->cells.front());
            n->cells.erase(n->cells.begin());
            return out;
        }
        if (n->kids[0]->ncells() < t_) ensure_branch_safe(n, 0);
        return pop_min(n->kids[0]);
    }

    // Borrow from a fat sibling, or merge with one — whichever applies. After
    // this returns, parent->kids[i] has at least t cells.
    void ensure_branch_safe(Node* parent, int i) {
        Node* child = parent->kids[static_cast<std::size_t>(i)];
        if (child->ncells() >= t_) return;

        Node* left_sib  = (i > 0)                 ? parent->kids[static_cast<std::size_t>(i - 1)] : nullptr;
        Node* right_sib = (i < parent->ncells())  ? parent->kids[static_cast<std::size_t>(i + 1)] : nullptr;

        if (left_sib && left_sib->ncells() >= t_) {
            // Rotate one entry through parent->cells[i-1].
            child->cells.insert(child->cells.begin(), std::move(parent->cells[static_cast<std::size_t>(i - 1)]));
            parent->cells[static_cast<std::size_t>(i - 1)] = std::move(left_sib->cells.back());
            left_sib->cells.pop_back();
            if (!child->leaf) {
                child->kids.insert(child->kids.begin(), left_sib->kids.back());
                left_sib->kids.pop_back();
            }
            return;
        }
        if (right_sib && right_sib->ncells() >= t_) {
            child->cells.push_back(std::move(parent->cells[static_cast<std::size_t>(i)]));
            parent->cells[static_cast<std::size_t>(i)] = std::move(right_sib->cells.front());
            right_sib->cells.erase(right_sib->cells.begin());
            if (!child->leaf) {
                child->kids.push_back(right_sib->kids.front());
                right_sib->kids.erase(right_sib->kids.begin());
            }
            return;
        }
        // No fat sibling — must merge.
        if (right_sib) merge_kids(parent, i);
        else           merge_kids(parent, i - 1);
    }

    // Fold parent->kids[i+1] into parent->kids[i], dropping parent->cells[i]
    // down between them.
    void merge_kids(Node* parent, int i) {
        Node* left  = parent->kids[static_cast<std::size_t>(i)];
        Node* right = parent->kids[static_cast<std::size_t>(i + 1)];

        left->cells.push_back(std::move(parent->cells[static_cast<std::size_t>(i)]));
        for (auto& e : right->cells) left->cells.push_back(std::move(e));
        if (!left->leaf) {
            for (Node* c : right->kids) left->kids.push_back(c);
            right->kids.clear();
        }
        parent->cells.erase(parent->cells.begin() + i);
        parent->kids.erase(parent->kids.begin() + i + 1);
        delete right;
    }

    // --- helpers ---------------------------------------------------------

    template <typename Visitor>
    void scan_rec(const Node* n, Visitor& v) const {
        const int k = n->ncells();
        for (int i = 0; i < k; ++i) {
            if (!n->leaf) scan_rec(n->kids[static_cast<std::size_t>(i)], v);
            v(n->cells[static_cast<std::size_t>(i)].key,
              n->cells[static_cast<std::size_t>(i)].value);
        }
        if (!n->leaf) scan_rec(n->kids[static_cast<std::size_t>(k)], v);
    }

    void dump_rec(const Node* n, int depth, std::ostream& os) const {
        os << std::string(static_cast<std::size_t>(depth) * 2, ' ') << "(";
        for (int i = 0; i < n->ncells(); ++i) {
            if (i) os << ' ';
            os << n->cells[static_cast<std::size_t>(i)].key;
        }
        os << ")" << (n->leaf ? "  [leaf]\n" : "\n");
        for (Node* c : n->kids) dump_rec(c, depth + 1, os);
    }

    // Recursive invariant checker. Returns the first violation as a string,
    // or "" if all good. `leaf_depth` is threaded through to enforce
    // "every leaf at the same depth".
    std::string check_rec(const Node* n, bool is_root, int depth, int& leaf_depth) const {
        const int k = n->ncells();
        if (k > 2*t_ - 1)                   return "node holds more than 2t-1 entries";
        if (!is_root && k < t_ - 1)         return "non-root holds fewer than t-1 entries";
        if (is_root && k < 1 && !n->leaf)   return "non-leaf root has no entries";

        for (int i = 1; i < k; ++i)
            if (!less_(n->cells[static_cast<std::size_t>(i - 1)].key,
                       n->cells[static_cast<std::size_t>(i)].key))
                return "cells not strictly increasing within a node";

        if (n->leaf) {
            if (leaf_depth == -1) leaf_depth = depth;
            else if (leaf_depth != depth) return "leaves at differing depths";
            if (!n->kids.empty()) return "leaf carries children";
            return "";
        }
        if (static_cast<int>(n->kids.size()) != k + 1)
            return "internal node has wrong number of children";

        for (int i = 0; i <= k; ++i) {
            const Node* c = n->kids[static_cast<std::size_t>(i)];
            for (const Entry& e : c->cells) {
                if (i < k && !less_(e.key, n->cells[static_cast<std::size_t>(i)].key))
                    return "child key not strictly less than parent separator";
                if (i > 0 && !less_(n->cells[static_cast<std::size_t>(i - 1)].key, e.key))
                    return "child key not strictly greater than parent separator";
            }
            std::string e = check_rec(c, /*is_root=*/false, depth + 1, leaf_depth);
            if (!e.empty()) return e;
        }
        return "";
    }
};

}  // namespace kt
