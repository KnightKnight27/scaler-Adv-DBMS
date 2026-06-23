#pragma once

#include <cstddef>
#include <vector>

#include "common/types.h"
#include "index/bplus_page.h"
#include "storage/buffer_pool.h"

namespace minidb {

// A page-backed B+Tree mapping an int64 key -> RID, used as a table's primary
// index. Internal nodes route; leaves hold (key -> RID) and are singly linked
// (next_leaf) so range scans walk leaves sequentially. Implemented in the
// Lehman-style "data only in leaves" form taught in the index lectures.
//
// `order` caps keys-per-node before a split; 0 means "use the physical page
// capacity" (real fanout, ~hundreds). Tests pass a small order to force splits
// and multi-level trees with few keys.
class BPlusTree {
public:
    BPlusTree(BufferPool* bp, PageId root, std::size_t order = 0);

    // Create an empty tree (a single empty leaf) and return its root page id.
    static PageId create(BufferPool* bp);

    PageId root_page() const { return root_; }

    // Insert key->rid. Returns false if the key already exists (primary key: no
    // duplicates). May change the root (see root_page()).
    bool insert(BTKey key, RID rid);

    // Point lookup. Returns true and sets `out` if found.
    bool search(BTKey key, RID& out) const;

    // Remove a key (no node merging; correctness preserved, see LEARNING.md).
    bool erase(BTKey key);

    // Number of levels (1 = just a leaf root). For demos/tests.
    std::size_t height() const;

    // Forward range scan over keys in [lo, hi] (bounds optionally exclusive),
    // walking the leaf chain.
    class RangeIterator {
    public:
        RangeIterator(BufferPool* bp, PageId leaf, int idx, BTKey hi, bool hi_inclusive)
            : bp_(bp), leaf_(leaf), idx_(idx), hi_(hi), hi_incl_(hi_inclusive) {}
        bool next(BTKey& key, RID& rid);
    private:
        BufferPool* bp_;
        PageId      leaf_;
        int         idx_;
        BTKey       hi_;
        bool        hi_incl_;
    };
    RangeIterator range(BTKey lo, BTKey hi, bool lo_inclusive = true, bool hi_inclusive = true) const;

private:
    // Result of a child insert that overflowed and split.
    struct Split {
        bool   happened = false;
        BTKey  sep_key  = 0;
        PageId right     = INVALID_PAGE_ID;
    };

    Split insert_rec(PageId node, BTKey key, RID rid, bool& inserted);

    std::size_t leaf_max() const {
        return order_ ? order_ : BPlusNode::leaf_capacity() - 1;
    }
    std::size_t internal_max() const {
        return order_ ? order_ : BPlusNode::internal_capacity() - 1;
    }

    BufferPool* bp_;
    PageId      root_;
    std::size_t order_;
};

} // namespace minidb
