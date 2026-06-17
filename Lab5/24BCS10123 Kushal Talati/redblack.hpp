// Lab 5 — Red-Black Tree (header-only, generic ordered map)
// 24BCS10123  Kushal Talati
//
// kt::OrderedIndex<Key, Value, Compare> is a CLRS-flavoured red-black tree.
// The five RB invariants are enforced after every mutation and an O(n)
// validate() returns a human-readable description of the first violation
// it finds (or std::nullopt if healthy).
//
// Implementation choices that differ from the typical textbook code:
//   * A shared sentinel `bottom_` represents NIL — keeps the fix-up
//     branches free of null checks. The sentinel is repaired at the end
//     of remove() so erase never leaves dangling parent state on it.
//   * The recolour-and-rotate cases for insert / erase are written out
//     symmetrically (`up_is_lo` mirror) instead of factored through a
//     helper, because the symmetry argument is the easiest way to
//     convince yourself the mirror is correct.
//   * Two recursive helpers (`walk_in_order_rec`, `validate_rec`) are
//     used instead of an iterative stack — recursion depth is O(log n)
//     for a well-formed RB tree, so a stack overflow would itself be a
//     bug worth seeing.

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

enum class NodeTint : unsigned char { Crimson, Onyx };   // RED, BLACK

template <typename Key, typename Value, typename Compare = std::less<Key>>
class OrderedIndex {
public:
    OrderedIndex()
        : bottom_(new Node{}), apex_(bottom_), entries_(0) {
        bottom_->tint = NodeTint::Onyx;
        bottom_->up = bottom_->lo = bottom_->hi = bottom_;
    }

    OrderedIndex(const OrderedIndex&)            = delete;
    OrderedIndex& operator=(const OrderedIndex&) = delete;
    OrderedIndex(OrderedIndex&&)                 = delete;
    OrderedIndex& operator=(OrderedIndex&&)      = delete;

    ~OrderedIndex() {
        burn(apex_);
        delete bottom_;
    }

    // --- modifiers --------------------------------------------------------

    // Insert or overwrite. Returns true when a new node was created.
    bool set(const Key& k, const Value& v) {
        Node* parent = bottom_;
        Node* cur    = apex_;
        while (cur != bottom_) {
            parent = cur;
            if      (less_(k, cur->key)) cur = cur->lo;
            else if (less_(cur->key, k)) cur = cur->hi;
            else { cur->value = v; return false; }
        }
        Node* fresh = new Node{k, v, NodeTint::Crimson, parent, bottom_, bottom_};
        if      (parent == bottom_)         apex_ = fresh;
        else if (less_(k, parent->key))     parent->lo = fresh;
        else                                parent->hi = fresh;
        ++entries_;
        rebalance_after_insert(fresh);
        return true;
    }

    // Remove the entry with this key. Returns true if it was present.
    bool remove(const Key& k) {
        Node* doomed = locate(k);
        if (doomed == bottom_) return false;
        --entries_;

        Node* spliced     = doomed;
        NodeTint lost     = doomed->tint;
        Node* gap;                                  // node that now sits where doomed was

        if (doomed->lo == bottom_) {
            gap = doomed->hi;
            replace_in_parent(doomed, doomed->hi);
        } else if (doomed->hi == bottom_) {
            gap = doomed->lo;
            replace_in_parent(doomed, doomed->lo);
        } else {
            spliced = subtree_min(doomed->hi);      // in-order successor
            lost    = spliced->tint;
            gap     = spliced->hi;
            if (spliced->up == doomed) {
                gap->up = spliced;                  // sentinel may temporarily carry a parent
            } else {
                replace_in_parent(spliced, spliced->hi);
                spliced->hi      = doomed->hi;
                spliced->hi->up  = spliced;
            }
            replace_in_parent(doomed, spliced);
            spliced->lo      = doomed->lo;
            spliced->lo->up  = spliced;
            spliced->tint    = doomed->tint;
        }

        delete doomed;
        if (lost == NodeTint::Onyx) rebalance_after_remove(gap);
        bottom_->up = bottom_;                      // scrub sentinel's parent — pure hygiene
        return true;
    }

    // --- queries ----------------------------------------------------------

    bool has(const Key& k) const { return locate(k) != bottom_; }

    std::optional<Value> get(const Key& k) const {
        const Node* n = locate(k);
        if (n == bottom_) return std::nullopt;
        return n->value;
    }

    // Throws std::out_of_range when missing — convenient for assertions.
    Value& fetch(const Key& k) {
        Node* n = locate(k);
        if (n == bottom_) throw std::out_of_range("OrderedIndex::fetch — missing key");
        return n->value;
    }

    std::size_t length() const noexcept { return entries_; }
    bool        empty()  const noexcept { return entries_ == 0; }

    // Sorted (key, value) traversal. Recursive — call depth is O(log n) for
    // a well-formed RB tree, which the invariants guarantee.
    template <typename Visitor>
    void walk(Visitor&& visit) const {
        walk_in_order_rec(apex_, visit);
    }

    // Level-order pretty printer for debugging.
    void render(std::ostream& os) const {
        if (apex_ == bottom_) { os << "(empty)\n"; return; }
        // Level-order without std::queue: keep two small vectors.
        std::size_t level = 0;
        std::vector<Node*> cur{apex_}, nxt;
        while (!cur.empty()) {
            os << "  L" << level << ": ";
            for (Node* n : cur) {
                os << n->key << "(" << (n->tint == NodeTint::Crimson ? 'R' : 'B') << ") ";
                if (n->lo != bottom_) nxt.push_back(n->lo);
                if (n->hi != bottom_) nxt.push_back(n->hi);
            }
            os << "\n";
            cur.swap(nxt); nxt.clear(); ++level;
        }
    }

    // Returns std::nullopt if every red-black invariant + BST ordering holds;
    // otherwise the first problem found. Designed to be called after every
    // mutation as a sanity check.
    std::optional<std::string> validate() const {
        if (apex_ != bottom_ && apex_->tint != NodeTint::Onyx)
            return std::string("inv-2: apex is not black");
        std::string err;
        validate_rec(apex_, /*lo_bound=*/nullptr, /*hi_bound=*/nullptr, err);
        if (err.empty()) return std::nullopt;
        return err;
    }

private:
    struct Node {
        Key       key{};
        Value     value{};
        NodeTint  tint = NodeTint::Crimson;
        Node*     up   = nullptr;
        Node*     lo   = nullptr;
        Node*     hi   = nullptr;
    };

    Node*       bottom_;     // shared black sentinel (NIL)
    Node*       apex_;       // tree root
    std::size_t entries_;
    Compare     less_{};

    // --- traversal helpers -----------------------------------------------

    Node* locate(const Key& k) {
        Node* cur = apex_;
        while (cur != bottom_) {
            if      (less_(k, cur->key)) cur = cur->lo;
            else if (less_(cur->key, k)) cur = cur->hi;
            else return cur;
        }
        return bottom_;
    }
    const Node* locate(const Key& k) const {
        const Node* cur = apex_;
        while (cur != bottom_) {
            if      (less_(k, cur->key)) cur = cur->lo;
            else if (less_(cur->key, k)) cur = cur->hi;
            else return cur;
        }
        return bottom_;
    }
    Node* subtree_min(Node* n) const {
        while (n->lo != bottom_) n = n->lo;
        return n;
    }
    void burn(Node* n) {
        if (n == bottom_) return;
        burn(n->lo);
        burn(n->hi);
        delete n;
    }

    // --- rotations --------------------------------------------------------
    // Pivot around `n`: the higher-side child of `n` becomes its parent.
    void pivot_higher(Node* n) {
        Node* r = n->hi;
        n->hi = r->lo;
        if (r->lo != bottom_) r->lo->up = n;
        r->up = n->up;
        if      (n->up == bottom_)        apex_ = r;
        else if (n == n->up->lo)          n->up->lo = r;
        else                              n->up->hi = r;
        r->lo = n;
        n->up = r;
    }
    void pivot_lower(Node* n) {
        Node* l = n->lo;
        n->lo = l->hi;
        if (l->hi != bottom_) l->hi->up = n;
        l->up = n->up;
        if      (n->up == bottom_)        apex_ = l;
        else if (n == n->up->hi)          n->up->hi = l;
        else                              n->up->lo = l;
        l->hi = n;
        n->up = l;
    }

    // Replace the subtree rooted at `victim` with the subtree rooted at `repl`.
    void replace_in_parent(Node* victim, Node* repl) {
        if      (victim->up == bottom_)       apex_ = repl;
        else if (victim == victim->up->lo)    victim->up->lo = repl;
        else                                  victim->up->hi = repl;
        repl->up = victim->up;
    }

    // --- fix-ups ----------------------------------------------------------

    void rebalance_after_insert(Node* z) {
        while (z->up->tint == NodeTint::Crimson) {
            Node* p = z->up;
            Node* g = p->up;
            if (p == g->lo) {                              // parent on the lower side
                Node* u = g->hi;                           // uncle
                if (u->tint == NodeTint::Crimson) {        // case 1 — recolour
                    p->tint = u->tint = NodeTint::Onyx;
                    g->tint = NodeTint::Crimson;
                    z = g;
                } else {
                    if (z == p->hi) {                      // case 2 — zig-zag
                        z = p;
                        pivot_higher(z);
                        p = z->up;
                    }
                    p->tint = NodeTint::Onyx;              // case 3 — straight
                    g->tint = NodeTint::Crimson;
                    pivot_lower(g);
                }
            } else {                                        // mirror
                Node* u = g->lo;
                if (u->tint == NodeTint::Crimson) {
                    p->tint = u->tint = NodeTint::Onyx;
                    g->tint = NodeTint::Crimson;
                    z = g;
                } else {
                    if (z == p->lo) {
                        z = p;
                        pivot_lower(z);
                        p = z->up;
                    }
                    p->tint = NodeTint::Onyx;
                    g->tint = NodeTint::Crimson;
                    pivot_higher(g);
                }
            }
        }
        apex_->tint = NodeTint::Onyx;
    }

    void rebalance_after_remove(Node* x) {
        while (x != apex_ && x->tint == NodeTint::Onyx) {
            if (x == x->up->lo) {
                Node* s = x->up->hi;                       // sibling
                if (s->tint == NodeTint::Crimson) {        // case 1
                    s->tint     = NodeTint::Onyx;
                    x->up->tint = NodeTint::Crimson;
                    pivot_higher(x->up);
                    s = x->up->hi;
                }
                if (s->lo->tint == NodeTint::Onyx && s->hi->tint == NodeTint::Onyx) {
                    s->tint = NodeTint::Crimson;           // case 2
                    x = x->up;
                } else {
                    if (s->hi->tint == NodeTint::Onyx) {   // case 3
                        s->lo->tint = NodeTint::Onyx;
                        s->tint     = NodeTint::Crimson;
                        pivot_lower(s);
                        s = x->up->hi;
                    }
                    s->tint     = x->up->tint;             // case 4
                    x->up->tint = NodeTint::Onyx;
                    s->hi->tint = NodeTint::Onyx;
                    pivot_higher(x->up);
                    x = apex_;
                }
            } else {                                        // mirror
                Node* s = x->up->lo;
                if (s->tint == NodeTint::Crimson) {
                    s->tint     = NodeTint::Onyx;
                    x->up->tint = NodeTint::Crimson;
                    pivot_lower(x->up);
                    s = x->up->lo;
                }
                if (s->hi->tint == NodeTint::Onyx && s->lo->tint == NodeTint::Onyx) {
                    s->tint = NodeTint::Crimson;
                    x = x->up;
                } else {
                    if (s->lo->tint == NodeTint::Onyx) {
                        s->hi->tint = NodeTint::Onyx;
                        s->tint     = NodeTint::Crimson;
                        pivot_higher(s);
                        s = x->up->lo;
                    }
                    s->tint     = x->up->tint;
                    x->up->tint = NodeTint::Onyx;
                    s->lo->tint = NodeTint::Onyx;
                    pivot_lower(x->up);
                    x = apex_;
                }
            }
        }
        x->tint = NodeTint::Onyx;
    }

    // --- in-order walk (recursive — depth is O(log n) on a healthy tree) -

    template <typename Visitor>
    void walk_in_order_rec(const Node* n, Visitor& v) const {
        if (n == bottom_) return;
        walk_in_order_rec(n->lo, v);
        v(n->key, n->value);
        walk_in_order_rec(n->hi, v);
    }

    // --- invariant validator ---------------------------------------------
    // Returns the black-height of the subtree; on the first violation, writes
    // a description into `err` and stops checking further. `lo_bound` and
    // `hi_bound` carry the BST open-range constraints.
    int validate_rec(const Node* n,
                     const Key* lo_bound, const Key* hi_bound,
                     std::string& err) const {
        if (n == bottom_) return 1;
        if (lo_bound && !less_(*lo_bound, n->key) && err.empty())
            err = "bst: key not greater than lo bound";
        if (hi_bound && !less_(n->key, *hi_bound) && err.empty())
            err = "bst: key not less than hi bound";
        if (n->tint == NodeTint::Crimson) {
            if ((n->lo->tint == NodeTint::Crimson || n->hi->tint == NodeTint::Crimson) && err.empty())
                err = "inv-4: red node has a red child";
        }
        int blh_lo = validate_rec(n->lo, lo_bound, &n->key, err);
        int blh_hi = validate_rec(n->hi, &n->key,  hi_bound, err);
        if (blh_lo != blh_hi && err.empty())
            err = "inv-5: differing black-heights";
        return blh_lo + (n->tint == NodeTint::Onyx ? 1 : 0);
    }
};

}  // namespace kt
