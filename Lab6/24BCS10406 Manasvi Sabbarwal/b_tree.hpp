// Lab 6 — B-Tree index, ADBMS.
// Manasvi Sabbarwal - 24BCS10406
//
// A templated B-Tree of minimum degree t, kept in memory but shaped exactly
// the way SQLite / Postgres / InnoDB shape their on-disk index pages: every
// node holds between t-1 and 2t-1 keys, every internal node fans out into
// (#keys + 1) children, every leaf is at the same depth.
//
// Supports the four operations a DB index needs:
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

    BTree(const BTree&)            = delete;
    BTree& operator=(const BTree&) = delete;
    BTree(BTree&&)                 = delete;
    BTree& operator=(BTree&&)      = delete;

    bool put(const Key& k, const Row& r) {
        if (!root_) {
            root_ = std::make_unique<Node>(true);
            root_->keys.push_back(k);
            root_->rows.push_back(r);
            ++count_;
            return true;
        }
        if (is_full(*root_)) {
            auto fresh   = std::make_unique<Node>(false);
            fresh->kids.push_back(std::move(root_));
            split(*fresh, 0);
            root_ = std::move(fresh);
        }
        return descend_insert(*root_, k, r);
    }

    bool remove(const Key& k) {
        if (!root_) return false;
        const bool gone = descend_erase(*root_, k);
        if (root_->keys.empty()) {
            if (root_->leaf) root_.reset();
            else             root_ = std::move(root_->kids[0]);
        }
        if (gone) --count_;
        return gone;
    }

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

    template <typename Fn>
    void for_each(Fn&& fn) const {
        if (root_) walk(*root_, fn);
    }

    void dump(std::ostream& os) const {
        if (!root_) { os << "(empty index)\n"; return; }
        dump_rec(*root_, 0, os);
    }

    std::string audit() const {
        if (!root_) return "";
        int leaf_depth = -1;
        return audit_rec(*root_, true, 0, leaf_depth);
    }

private:
    struct Node {
        std::vector<Key>                   keys;
        std::vector<Row>                   rows;
        std::vector<std::unique_ptr<Node>> kids;
        bool                               leaf;
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
        int size() const { return static_cast<int>(keys.size()); }
    };

    std::unique_ptr<Node> root_;
    int                   t_;
    std::size_t           count_ = 0;
    Compare               cmp_{};

    bool lt(const Key& a, const Key& b) const { return cmp_(a, b); }
    bool eq(const Key& a, const Key& b) const { return !lt(a, b) && !lt(b, a); }

    bool is_full(const Node& n) const { return n.size() == 2 * t_ - 1; }

    int floor_slot(const Node& n, const Key& k) const {
        int i = 0;
        while (i < n.size() && lt(n.keys[i], k)) ++i;
        return i;
    }

    void split(Node& parent, int idx) {
        Node& y = *parent.kids[idx];
        auto z_owned = std::make_unique<Node>(y.leaf);
        Node& z = *z_owned;

        const int median = t_ - 1;

        z.keys.assign(std::make_move_iterator(y.keys.begin() + t_),
                      std::make_move_iterator(y.keys.end()));
        z.rows.assign(std::make_move_iterator(y.rows.begin() + t_),
                      std::make_move_iterator(y.rows.end()));
        if (!y.leaf) {
            z.kids.reserve(t_);
            for (int i = t_; i < static_cast<int>(y.kids.size()); ++i)
                z.kids.push_back(std::move(y.kids[i]));
            y.kids.erase(y.kids.begin() + t_, y.kids.end());
        }
        Key pivot_k = std::move(y.keys[median]);
        Row pivot_r = std::move(y.rows[median]);
        y.keys.erase(y.keys.begin() + median, y.keys.end());
        y.rows.erase(y.rows.begin() + median, y.rows.end());

        parent.kids.insert(parent.kids.begin() + idx + 1, std::move(z_owned));
        parent.keys.insert(parent.keys.begin() + idx, std::move(pivot_k));
        parent.rows.insert(parent.rows.begin() + idx, std::move(pivot_r));
    }

    bool descend_insert(Node& n, const Key& k, const Row& r) {
        int i = floor_slot(n, k);
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
            if (eq(n.keys[i], k)) { n.rows[i] = r; return false; }
            if (lt(n.keys[i], k)) ++i;
        }
        return descend_insert(*n.kids[i], k, r);
    }

    bool descend_erase(Node& n, const Key& k) {
        int i = floor_slot(n, k);
        const bool hit = (i < n.size()) && eq(n.keys[i], k);

        if (hit && n.leaf) {
            n.keys.erase(n.keys.begin() + i);
            n.rows.erase(n.rows.begin() + i);
            return true;
        }
        if (hit) return erase_internal(n, i);
        if (n.leaf) return false;

        const bool taking_last = (i == n.size());
        if (n.kids[i]->size() < t_) refill(n, i);
        if (taking_last && i > n.size()) --i;
        return descend_erase(*n.kids[i], k);
    }

    bool erase_internal(Node& n, int i) {
        Node& left  = *n.kids[i];
        Node& right = *n.kids[i + 1];
        if (left.size() >= t_) {
            auto [pk, pr] = pop_back(left);
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
        merge_at(n, i);
        Key target = left.keys[t_ - 1];
        return descend_erase(left, target);
    }

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
        return pop_back(*n.kids[std::min<int>(last, n.size())]);
    }

    void refill(Node& parent, int i) {
        Node& child = *parent.kids[i];
        if (child.size() >= t_) return;

        Node* left  = (i > 0)             ? parent.kids[i - 1].get() : nullptr;
        Node* right = (i < parent.size()) ? parent.kids[i + 1].get() : nullptr;

        if (left && left->size() >= t_) {
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
        if (right) merge_at(parent, i);
        else       merge_at(parent, i - 1);
    }

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
        parent.kids.erase(parent.kids.begin() + idx + 1);
    }

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
            std::string err = audit_rec(c, false, depth + 1, leaf_depth);
            if (!err.empty()) return err;
        }
        return "";
    }
};

template <typename K, typename V, typename C>
std::string dump_to_string(const BTree<K, V, C>& t) {
    std::ostringstream os;
    t.dump(os);
    return os.str();
}

}  // namespace adbms::lab6
