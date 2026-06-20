#pragma once
#include <vector>
#include <utility>
#include "common/types.h"
#include "storage/buffer_pool.h"

namespace minidb {

// ---------------------------------------------------------------------------
// A disk-resident B+Tree mapping an INTEGER key to a RID (row location).
//
// Every node is exactly one page and is fetched through the buffer pool, so the
// index participates in the same caching/eviction machinery as table data.
// Keys are 32-bit integers (the common primary-key case); we document this as a
// deliberate scoping decision rather than supporting every type in the tree.
//
// Node layout inside a page (we cast page->data() to these structs):
//   - Internal node: [header][ key_0 .. key_{n-1} ][ child_0 .. child_n ]
//                    child_i holds keys < key_i ; child_n holds keys >= key_{n-1}
//   - Leaf node:     [header][ key_0 .. key_{n-1} ][ rid_0  .. rid_{n-1}  ]
//                    leaves are linked left-to-right via header.next for scans.
// ---------------------------------------------------------------------------

// Fan-out chosen to keep both node structs comfortably under PAGE_SIZE.
constexpr int BTREE_ORDER = 200;

struct BTreeNode {
    int32_t   is_leaf;                 // 1 = leaf, 0 = internal
    int32_t   size;                    // number of keys currently stored
    page_id_t next;                    // leaf: next leaf page; else INVALID
    page_id_t self;                    // this node's own page id

    int32_t   keys[BTREE_ORDER];       // sorted keys

    // The two node kinds reuse the trailing space differently.
    union {
        page_id_t children[BTREE_ORDER + 1]; // internal: child page ids
        RID       rids[BTREE_ORDER];         // leaf: row locations
    };
};
static_assert(sizeof(BTreeNode) <= PAGE_SIZE, "B+Tree node must fit in a page");

class BPlusTree {
public:
    explicit BPlusTree(BufferPoolManager *bpm);

    // Point lookup. On success sets *out_rid and returns true.
    bool search(int32_t key, RID *out_rid);

    // Insert key->rid. Duplicate keys are rejected (primary-key semantics).
    // Returns false if the key already exists.
    bool insert(int32_t key, const RID &rid);

    // Remove key. Lazy deletion: we delete the entry from its leaf but do not
    // merge/rebalance underfull nodes (documented trade-off). Returns true if
    // the key was present.
    bool remove(int32_t key);

    // Range scan over [low, high] inclusive, following leaf links. Returns the
    // matching RIDs in key order. Used by index range predicates.
    std::vector<RID> range(int32_t low, int32_t high);

    page_id_t root_page_id() const { return root_page_id_; }
    bool empty() const { return root_page_id_ == INVALID_PAGE_ID; }

private:
    BTreeNode *as_node(Page *p) { return reinterpret_cast<BTreeNode *>(p->data()); }

    // Descend from the root to the leaf that should contain `key`, recording the
    // path of internal page ids so a split can propagate upward.
    page_id_t find_leaf(int32_t key, std::vector<page_id_t> *path);

    // Insert (key,child) into an internal node, splitting and recursing upward
    // as needed. `path` is the stack of ancestors built by find_leaf.
    void insert_into_parent(std::vector<page_id_t> &path, page_id_t left,
                            int32_t key, page_id_t right);

    BufferPoolManager *bpm_;
    page_id_t          root_page_id_{INVALID_PAGE_ID};
};

} // namespace minidb
