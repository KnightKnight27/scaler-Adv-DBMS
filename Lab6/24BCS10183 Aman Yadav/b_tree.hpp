// Lab 6 — B-Tree index, ADBMS.
// Aman Yadav · 24BCS10183
//
// A templated B-Tree of minimum degree t, kept in memory but shaped exactly
// the way SQLite / Postgres / InnoDB shape their on-disk index pages: every
// node holds between t-1 and 2t-1 keys, every internal node fans out into
// (#keys + 1) children, every leaf is at the same depth.
//
// Supports the four operations a DB index actually needs:
//   * put(key, row)       — insert or overwrite, returns true iff a new key
//   * get(key)            — std::optional<Row>, no exceptions on miss
//   * remove(key)         — returns true iff the key was present
//   * for_each(fn)        — sorted (key, row) walk for range scans
//
// Children are owned with std::unique_ptr so the tree's memory shape lives
// inside the type system — no manual delete, no leaked nodes if an insert
// throws halfway through. Implementation follows CLRS chapter 18 with the
// proactive-split-on-the-way-down trick for insert and the six-case erase.
//
// A built-in audit() walks the tree and returns the first invariant it
// finds broken (or "" if healthy). The demo calls it after every mutation.

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace adbms::lab6 {

template <typename Key, typename Row, typename Compare = std::less<Key>>
class BTree {
public:
    explicit BTree(int min_degree = 3)
        : t_(min_degree < 2 ? 2 : min_degree) {}

    // The tree owns its nodes uniquely — no copies, no moves. A DB index
    // would also be pinned to its buffer pool, so this matches.
    BTree(const BTree&)            = delete;
    BTree& operator=(const BTree&) = delete;
    BTree(BTree&&)                 = delete;
    BTree& operator=(BTree&&)      = delete;

    // --- modifiers -------------------------------------------------------

    // Returns true on a fresh insert, false if the key was already present
    // and got its row overwritten.
    bool put(const Key& k, const Row& r) {
        if (!root_) {
            root_ = std::make_unique<Node>(/*leaf=*/true);
            root_->keys.push_back(k);
            root_->rows.push_back(r);
            ++count_;
            return true;
        }
        // CLRS proactive root split: if the root itself is full, grow the
        // tree one level up *before* descending. This is the only path
        // that ever increases the tree's height.
        if (is_full(*root_)) {
            auto fresh   = std::make_unique<Node>(/*leaf=*/false);
            fresh->kids.push_back(std::move(root_));
            split(*fresh, 0);
            root_ = std::move(fresh);
        }
        return descend_insert(*root_, k, r);
    }

    bool remove(const Key& k) {
        if (!root_) return false;
        const bool gone = descend_erase(*root_, k);
        // After erase the root might be left with zero keys. If it was an
        // internal node, drop a level (its only child becomes the new root).
        // If it was a leaf, the tree is empty.
        if (root_->keys.empty()) {
            if (root_->leaf) root_.reset();
            else             root_ = std::move(root_->kids[0]);
        }
        if (gone) --count_;
        return gone;
    }

    // --- queries ---------------------------------------------------------

    std::optional<Row> get(const Key& k) const {
        const Node* n = root_.get();
        while (n) {
            const int i = floor_slot(*n, k);
            if (i < n->size() && !lt(k, n->keys[i]) && !lt(n->keys[i], k))
                return n->rows[i];
            if (n->leaf) return std::nullopt;
            n = n->kids[i].get();
        }
        return std::nullopt;
    }

    bool          has(const Key& k) const { return get(k).has_value(); }
    std::size_t   size()             const { return count_; }
    bool          empty()            const { return count_ == 0; }
    int           min_degree()       const { return t_; }

    // Sorted walk. `fn` is called on every (key, row) in ascending order;
    // this is the building block a range scan or a CSV export would use.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        if (root_) walk(*root_, fn);
    }

    // Indented dump for eyeballing tree shape during debugging.
    void dump(std::ostream& os) const {
        if (!root_) { os << "(empty index)\n"; return; }
        dump_rec(*root_, 0, os);
    }

    // Returns "" when every invariant holds. Otherwise the message names
    // the first violation encountered. Cheap enough to run after every
    // mutation while exercising the implementation.
    std::string audit() const {
        if (!root_) return "";
        int leaf_depth = -1;
        return audit_rec(*root_, /*is_root=*/true, /*depth=*/0, leaf_depth);
    }

private:
    // ---------------- node ----------------------------------------------
    struct Node {
        std::vector<Key>                   keys;
        std::vector<Row>                   rows;
        std::vector<std::unique_ptr<Node>> kids;   // empty iff leaf
        bool                               leaf;
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
        int size() const { return static_cast<int>(keys.size()); }
    };

    std::unique_ptr<Node> root_;
    int                   t_;
    std::size_t           count_ = 0;
    Compare               cmp_{};

    // Comparator wrappers so we don't repeat the `cmp_(...)` pattern.
    bool lt(const Key& a, const Key& b) const { return cmp_(a, b); }
    bool eq(const Key& a, const Key& b) const { return !lt(a, b) && !lt(b, a); }

    bool is_full(const Node& n) const { return n.size() == 2 * t_ - 1; }

    // First slot i such that n.keys[i] >= k (or n.size() if none). Linear
    // scan is intentional — node sizes are O(t) and cache-friendly, so a
    // tight loop beats std::lower_bound for the sizes we care about (t ≤ 64).
    int floor_slot(const Node& n, const Key& k) const {
        int i = 0;
        while (i < n.size() && lt(n.keys[i], k)) ++i;
        return i;
    }

    // ---------------- split ---------------------------------------------
    // Split parent.kids[idx] which is required to be full (2t-1 keys).
    // The median key is promoted into the parent at slot `idx`; the upper
    // half goes into a brand-new sibling placed at slot `idx+1`.
    void split(Node& parent, int idx) {
        Node& y = *parent.kids[idx];
        auto z_owned = std::make_unique<Node>(y.leaf);
        Node& z = *z_owned;

        const int median = t_ - 1;

        // Upper half of keys/rows -> z.
        z.keys.assign(std::make_move_iterator(y.keys.begin() + t_),
                      std::make_move_iterator(y.keys.end()));
        z.rows.assign(std::make_move_iterator(y.rows.begin() + t_),
                      std::make_move_iterator(y.rows.end()));
        // Upper half of children -> z (if internal).
        if (!y.leaf) {
            z.kids.reserve(t_);
            for (int i = t_; i < static_cast<int>(y.kids.size()); ++i)
                z.kids.push_back(std::move(y.kids[i]));
            y.kids.erase(y.kids.begin() + t_, y.kids.end());
        }
        // Capture the median before shrinking y.
        Key pivot_k = std::move(y.keys[median]);
        Row pivot_r = std::move(y.rows[median]);
        y.keys.erase(y.keys.begin() + median, y.keys.end());
        y.rows.erase(y.rows.begin() + median, y.rows.end());

        parent.kids.insert(parent.kids.begin() + idx + 1, std::move(z_owned));
        parent.keys.insert(parent.keys.begin() + idx, std::move(pivot_k));
        parent.rows.insert(parent.rows.begin() + idx, std::move(pivot_r));
    }

    // ---------------- insert --------------------------------------------
    // `n` is guaranteed to be non-full. Returns true if a new key was added.
    bool descend_insert(Node& n, const Key& k, const Row& r) {
        int i = floor_slot(n, k);
        // Overwrite path — same key already at this slot.
        if (i < n.size() && eq(n.keys[i], k)) {
            n.rows[i] = r;
            return false;
        }
        if (n.leaf) {
            n.keys.insert(n.keys.begin() + i, k);
            n.rows.insert(n.rows.begin() + i, r);
            ++count_;
            return true;
        }
        if (is_full(*n.kids[i])) {
            split(n, i);
            // After the split, the median sits at n.keys[i]. The new key
            // either equals it (overwrite), goes left (i unchanged), or
            // goes right (advance i).
            if (eq(n.keys[i], k)) { n.rows[i] = r; return false; }
            if (lt(n.keys[i], k)) ++i;
        }
        return descend_insert(*n.kids[i], k, r);
    }

    // ---------------- erase (CLRS chapter 18) ----------------------------
    // Caller guarantees n has >= t keys, unless n is the root. Returns
    // true if a key was actually removed from the subtree at n.
    bool descend_erase(Node& n, const Key& k) {
        int i = floor_slot(n, k);
        const bool hit = (i < n.size()) && eq(n.keys[i], k);

        if (hit && n.leaf) {
            n.keys.erase(n.keys.begin() + i);
            n.rows.erase(n.rows.begin() + i);
            return true;
        }
        if (hit) return erase_internal(n, i);
        if (n.leaf) return false;                 // key not in tree

        // Need to descend into kids[i]. Make sure that child has >= t keys
        // first so we maintain the invariant for the recursive call.
        const bool taking_last = (i == n.size());
        if (n.kids[i]->size() < t_) refill(n, i);
        // refill may have merged kids[i] with its right sibling, shrinking
        // the parent's child count. If we were about to take the rightmost
        // child but it just merged into the previous slot, step back.
        if (taking_last && i > n.size()) --i;
        return descend_erase(*n.kids[i], k);
    }

    // The current node contains k at slot i and is internal. Three cases:
    //   (a) left child has spares  -> swap key with predecessor & recurse
    //   (b) right child has spares -> swap key with successor   & recurse
    //   (c) both children minimal  -> merge them through the separator,
    //                                 then erase from the merged child.
    bool erase_internal(Node& n, int i) {
        Node& left  = *n.kids[i];
        Node& right = *n.kids[i + 1];
        if (left.size() >= t_) {
            auto [pk, pr] = pop_back(left);   // recursive — may rebalance
            n.keys[i] = std::move(pk);
            n.rows[i] = std::move(pr);
            return true;
        }
        if (right.size() >= t_) {
            auto [sk, sr] = pop_front(right);
            n.keys[i] = std::move(sk);
            n.rows[i] = std::move(sr);
            return true;
        }
        // Case (c): both minimal. Merge children[i] and children[i+1]
        // together with n.keys[i] sandwiched between them. The original
        // key now sits at slot t-1 of the merged node — capture it before
        // recursing because that slot is about to move.
        merge_at(n, i);
        Key target = left.keys[t_ - 1];        // copy, not reference
        return descend_erase(left, target);
    }

    // Smallest entry in the subtree rooted at n. Caller guarantees n has
    // >= t keys; we maintain that as we descend through internal nodes.
    std::pair<Key, Row> pop_front(Node& n) {
        if (n.leaf) {
            Key k = std::move(n.keys.front());
            Row r = std::move(n.rows.front());
            n.keys.erase(n.keys.begin());
            n.rows.erase(n.rows.begin());
            return {std::move(k), std::move(r)};
        }
        if (n.kids[0]->size() < t_) refill(n, 0);
        return pop_front(*n.kids[0]);
    }
    std::pair<Key, Row> pop_back(Node& n) {
        if (n.leaf) {
            Key k = std::move(n.keys.back());
            Row r = std::move(n.rows.back());
            n.keys.pop_back();
            n.rows.pop_back();
            return {std::move(k), std::move(r)};
        }
        const int last = n.size();
        if (n.kids[last]->size() < t_) refill(n, last);
        // refill may have merged the rightmost child away.
        return pop_back(*n.kids[std::min<int>(last, n.size())]);
    }

    // Make sure parent.kids[i] has >= t keys, by either rotating a key
    // through `parent` from a fat sibling, or merging with a minimal one.
    void refill(Node& parent, int i) {
        Node& child = *parent.kids[i];
        if (child.size() >= t_) return;

        Node* left  = (i > 0)             ? parent.kids[i - 1].get() : nullptr;
        Node* right = (i < parent.size()) ? parent.kids[i + 1].get() : nullptr;

        if (left && left->size() >= t_) {
            // Rotate one (key, row, optional-child) leftward through parent.
            child.keys.insert(child.keys.begin(), std::move(parent.keys[i - 1]));
            child.rows.insert(child.rows.begin(), std::move(parent.rows[i - 1]));
            parent.keys[i - 1] = std::move(left->keys.back());
            parent.rows[i - 1] = std::move(left->rows.back());
            left->keys.pop_back();
            left->rows.pop_back();
            if (!child.leaf) {
                child.kids.insert(child.kids.begin(), std::move(left->kids.back()));
                left->kids.pop_back();
            }
            return;
        }
        if (right && right->size() >= t_) {
            child.keys.push_back(std::move(parent.keys[i]));
            child.rows.push_back(std::move(parent.rows[i]));
            parent.keys[i] = std::move(right->keys.front());
            parent.rows[i] = std::move(right->rows.front());
            right->keys.erase(right->keys.begin());
            right->rows.erase(right->rows.begin());
            if (!child.leaf) {
                child.kids.push_back(std::move(right->kids.front()));
                right->kids.erase(right->kids.begin());
            }
            return;
        }
        // No spares anywhere — merge with whichever sibling exists. Left
        // merge collapses (i-1, i) into i-1; right merge collapses (i, i+1)
        // into i.
        if (right) merge_at(parent, i);
        else       merge_at(parent, i - 1);
    }

    // Merge parent.kids[idx] (left) and parent.kids[idx+1] (right) into
    // one node holding 2t-1 keys; parent.keys[idx] drops between them.
    void merge_at(Node& parent, int idx) {
        Node& left  = *parent.kids[idx];
        Node& right = *parent.kids[idx + 1];

        left.keys.push_back(std::move(parent.keys[idx]));
        left.rows.push_back(std::move(parent.rows[idx]));
        for (auto& k : right.keys) left.keys.push_back(std::move(k));
        for (auto& r : right.rows) left.rows.push_back(std::move(r));
        if (!left.leaf) {
            for (auto& c : right.kids) left.kids.push_back(std::move(c));
        }
        parent.keys.erase(parent.keys.begin() + idx);
        parent.rows.erase(parent.rows.begin() + idx);
        parent.kids.erase(parent.kids.begin() + idx + 1);  // right is freed
    }

    // ---------------- traversal / dump / audit --------------------------
    template <typename Fn>
    void walk(const Node& n, Fn& fn) const {
        const int k = n.size();
        for (int i = 0; i < k; ++i) {
            if (!n.leaf) walk(*n.kids[i], fn);
            fn(n.keys[i], n.rows[i]);
        }
        if (!n.leaf) walk(*n.kids[k], fn);
    }

    static void dump_rec(const Node& n, int depth, std::ostream& os) {
        os << std::string(static_cast<std::size_t>(depth) * 2, ' ') << "{";
        for (int i = 0; i < n.size(); ++i) {
            if (i) os << ", ";
            os << n.keys[i];
        }
        os << "}" << (n.leaf ? "  <leaf>\n" : "\n");
        for (const auto& c : n.kids) dump_rec(*c, depth + 1, os);
    }

    std::string audit_rec(const Node& n, bool is_root, int depth,
                          int& leaf_depth) const {
        const int k = n.size();
        if (k > 2 * t_ - 1)
            return "node exceeds 2t-1 keys";
        if (!is_root && k < t_ - 1)
            return "non-root holds fewer than t-1 keys";
        if (is_root && !n.leaf && k < 1)
            return "internal root has zero keys";

        for (int i = 1; i < k; ++i)
            if (!lt(n.keys[i - 1], n.keys[i]))
                return "keys not strictly increasing in a node";

        if (n.leaf) {
            if (!n.kids.empty()) return "leaf has children";
            if (leaf_depth == -1)        leaf_depth = depth;
            else if (leaf_depth != depth) return "leaves at different depths";
            return "";
        }
        if (static_cast<int>(n.kids.size()) != k + 1)
            return "internal node child count != keys + 1";

        for (int i = 0; i <= k; ++i) {
            const Node& c = *n.kids[i];
            for (const Key& x : c.keys) {
                if (i < k && !lt(x, n.keys[i]))
                    return "child key not strictly < right separator";
                if (i > 0 && !lt(n.keys[i - 1], x))
                    return "child key not strictly > left separator";
            }
            std::string err = audit_rec(c, /*is_root=*/false,
                                        depth + 1, leaf_depth);
            if (!err.empty()) return err;
        }
        return "";
    }
};

// Convenience helper: turn the indented dump into a string (used by tests
// and the demo's "look at the shape" lines).
template <typename K, typename V, typename C>
std::string dump_to_string(const BTree<K, V, C>& t) {
    std::ostringstream os;
    t.dump(os);
    return os.str();
}

}  // namespace adbms::lab6
