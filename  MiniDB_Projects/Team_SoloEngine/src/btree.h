#pragma once

#include "buffer_pool.h"
#include "storage.h"

#include <cstdint>
#include <optional>
#include <vector>

// ─── Node capacity arithmetic ───────────────────────────────────────────────
// Every page has a 16-byte header.  Remaining bytes go to keys + values/children.
//
//   Leaf:     16 + 254*8 + 254*8 = 4080 ≤ 4096 ✓
//   Internal: 16 + 338*8 + 339*4 = 4076 (+4 align pad) = 4080 ≤ 4096 ✓

static constexpr int32_t LEAF_MAX_KEYS     = 254;
static constexpr int32_t INTERNAL_MAX_KEYS = 338;

// 16-byte header present in every page.
struct NodeHeader {
    int32_t   is_leaf;    // 1 = leaf, 0 = internal
    int32_t   num_keys;
    page_id_t next_leaf;  // leaf-only sibling pointer
    int32_t   _pad;
};
static_assert(sizeof(NodeHeader) == 16);

struct LeafNode {
    NodeHeader hdr;
    int64_t    keys  [LEAF_MAX_KEYS];
    int64_t    values[LEAF_MAX_KEYS];
};
static_assert(sizeof(LeafNode) <= PAGE_SIZE);

struct InternalNode {
    NodeHeader hdr;
    int64_t    keys    [INTERNAL_MAX_KEYS];
    page_id_t  children[INTERNAL_MAX_KEYS + 1];
};
static_assert(sizeof(InternalNode) <= PAGE_SIZE);

// ─── B+ Tree ───────────────────────────────────────────────────────────────

class BPlusTree {
public:
    explicit BPlusTree(BufferPoolManager *bpm);

    std::optional<int64_t> Search(int64_t key);
    void Insert(int64_t key, int64_t value);
    void Delete(int64_t key);  // simple shift-left; no rebalancing

    page_id_t RootPageId() const { return root_page_id_; }

private:
    BufferPoolManager *bpm_;
    page_id_t          root_page_id_{INVALID_PAGE_ID};

    struct SplitResult { page_id_t new_page; int64_t push_key; };

    // Pinning contract for the two helpers below:
    //   - The Page* passed IN is already pinned by the caller; these functions
    //     do NOT unpin it.
    //   - If a split occurs, the new right page is allocated, initialised, and
    //     immediately unpinned before returning.
    std::optional<SplitResult> InsertIntoLeaf(Page *leaf, int64_t key, int64_t value);
    std::optional<SplitResult> InsertIntoInternal(Page *node, int32_t child_idx,
                                                   int64_t push_key,
                                                   page_id_t right_child);
};
