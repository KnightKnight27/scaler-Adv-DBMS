#include "btree.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

// ─── page-to-node casts ──────────────────────────────────────────────────────

static LeafNode     *AsLeaf    (Page *p) { return reinterpret_cast<LeafNode    *>(p->data); }
static InternalNode *AsInternal(Page *p) { return reinterpret_cast<InternalNode*>(p->data); }

// ─── constructor ─────────────────────────────────────────────────────────────

BPlusTree::BPlusTree(BufferPoolManager *bpm) : bpm_(bpm) {
    page_id_t id;
    Page *p = bpm_->NewPage(id);
    if (!p) throw std::runtime_error("BPlusTree: cannot allocate root");
    LeafNode *root = AsLeaf(p);
    root->hdr.is_leaf   = 1;
    root->hdr.num_keys  = 0;
    root->hdr.next_leaf = INVALID_PAGE_ID;
    root->hdr._pad      = 0;
    bpm_->UnpinPage(id, true);
    root_page_id_ = id;
}

// ─── Search ──────────────────────────────────────────────────────────────────
// Pins at most 2 pages at a time (current + next).  Every pin is released
// before returning.

std::optional<int64_t> BPlusTree::Search(int64_t key) {
    page_id_t cur_id = root_page_id_;
    Page *cur = bpm_->FetchPage(cur_id);
    if (!cur) return std::nullopt;

    while (!AsLeaf(cur)->hdr.is_leaf) {
        InternalNode *n = AsInternal(cur);
        int32_t c = static_cast<int32_t>(
            std::upper_bound(n->keys, n->keys + n->hdr.num_keys, key) - n->keys);
        page_id_t next_id = n->children[c];
        bpm_->UnpinPage(cur_id, false);   // release parent before descending
        cur = bpm_->FetchPage(next_id);
        if (!cur) return std::nullopt;
        cur_id = next_id;
    }

    LeafNode *leaf = AsLeaf(cur);
    int32_t n = leaf->hdr.num_keys;
    int32_t pos = static_cast<int32_t>(
        std::lower_bound(leaf->keys, leaf->keys + n, key) - leaf->keys);
    std::optional<int64_t> result;
    if (pos < n && leaf->keys[pos] == key)
        result = leaf->values[pos];

    bpm_->UnpinPage(cur_id, false);
    return result;
}

// ─── InsertIntoLeaf ──────────────────────────────────────────────────────────
// Caller owns the pin on `leaf`.  If a split is needed, the right page is
// allocated here and immediately unpinned before we return.

std::optional<BPlusTree::SplitResult>
BPlusTree::InsertIntoLeaf(Page *leaf_page, int64_t key, int64_t value) {
    LeafNode *leaf = AsLeaf(leaf_page);
    int32_t n   = leaf->hdr.num_keys;
    int32_t pos = static_cast<int32_t>(
        std::lower_bound(leaf->keys, leaf->keys + n, key) - leaf->keys);

    // Overwrite existing key.
    if (pos < n && leaf->keys[pos] == key) {
        leaf->values[pos] = value;
        return std::nullopt;
    }

    // Room in leaf — simple insert.
    if (n < LEAF_MAX_KEYS) {
        for (int32_t i = n; i > pos; --i) {
            leaf->keys[i]   = leaf->keys[i - 1];
            leaf->values[i] = leaf->values[i - 1];
        }
        leaf->keys[pos]    = key;
        leaf->values[pos]  = value;
        leaf->hdr.num_keys = n + 1;
        return std::nullopt;
    }

    // Leaf is full — merge into a temporary array and split.
    int32_t total = n + 1;
    int64_t tmp_k[LEAF_MAX_KEYS + 1];
    int64_t tmp_v[LEAF_MAX_KEYS + 1];
    for (int32_t src = 0, dst = 0; dst < total; ++dst) {
        if (dst == pos) { tmp_k[dst] = key;              tmp_v[dst] = value; }
        else            { tmp_k[dst] = leaf->keys[src];  tmp_v[dst] = leaf->values[src]; ++src; }
    }

    int32_t mid = total / 2;   // left keeps [0, mid), right gets [mid, total)

    // Allocate and initialise right leaf, then immediately unpin.
    page_id_t right_id;
    Page *right_page = bpm_->NewPage(right_id);
    if (!right_page) throw std::runtime_error("BPlusTree: pool full (leaf split)");
    LeafNode *right       = AsLeaf(right_page);
    right->hdr.is_leaf    = 1;
    right->hdr.num_keys   = total - mid;
    right->hdr.next_leaf  = leaf->hdr.next_leaf;
    right->hdr._pad       = 0;
    for (int32_t i = 0; i < total - mid; ++i) {
        right->keys[i]   = tmp_k[mid + i];
        right->values[i] = tmp_v[mid + i];
    }
    bpm_->UnpinPage(right_id, true);   // ← right leaf released here

    // Update left leaf in-place.
    leaf->hdr.num_keys  = mid;
    leaf->hdr.next_leaf = right_id;
    for (int32_t i = 0; i < mid; ++i) {
        leaf->keys[i]   = tmp_k[i];
        leaf->values[i] = tmp_v[i];
    }

    return SplitResult{right_id, tmp_k[mid]};  // push-up key = first key of right leaf
}

// ─── InsertIntoInternal ──────────────────────────────────────────────────────
// Caller owns the pin on `node`.  If a split is needed, the right page is
// allocated here and immediately unpinned before we return.

std::optional<BPlusTree::SplitResult>
BPlusTree::InsertIntoInternal(Page *node_page, int32_t child_idx,
                               int64_t push_key, page_id_t right_child) {
    InternalNode *node = AsInternal(node_page);
    int32_t n = node->hdr.num_keys;

    // Room in node — simple insert.
    if (n < INTERNAL_MAX_KEYS) {
        for (int32_t i = n; i > child_idx; --i)
            node->keys[i] = node->keys[i - 1];
        for (int32_t i = n + 1; i > child_idx + 1; --i)
            node->children[i] = node->children[i - 1];
        node->keys[child_idx]         = push_key;
        node->children[child_idx + 1] = right_child;
        node->hdr.num_keys = n + 1;
        return std::nullopt;
    }

    // Node is full — merge into temporary arrays and split.
    // After insertion: total = n+1 keys, n+2 children.
    int32_t   total = n + 1;
    int64_t   tmp_k[INTERNAL_MAX_KEYS + 1];
    page_id_t tmp_c[INTERNAL_MAX_KEYS + 2];

    // Build tmp_c: original children with right_child inserted at child_idx+1.
    for (int32_t i = 0; i <= child_idx; ++i)  tmp_c[i]     = node->children[i];
    tmp_c[child_idx + 1] = right_child;
    for (int32_t i = child_idx + 1; i <= n; ++i)  tmp_c[i + 1] = node->children[i];

    // Build tmp_k: original keys with push_key inserted at child_idx.
    for (int32_t i = 0; i < child_idx; ++i)  tmp_k[i]      = node->keys[i];
    tmp_k[child_idx] = push_key;
    for (int32_t i = child_idx; i < n; ++i)  tmp_k[i + 1]  = node->keys[i];

    // The middle key is pushed up; it is NOT kept in either child.
    int32_t mid          = total / 2;
    int64_t new_push_key = tmp_k[mid];

    // Rewrite left node in-place.
    node->hdr.num_keys = mid;
    for (int32_t i = 0; i < mid; ++i)   node->keys[i]      = tmp_k[i];
    for (int32_t i = 0; i <= mid; ++i)  node->children[i]  = tmp_c[i];

    // Allocate right node and immediately unpin.
    page_id_t right_id;
    Page *right_page = bpm_->NewPage(right_id);
    if (!right_page) throw std::runtime_error("BPlusTree: pool full (internal split)");
    InternalNode *right   = AsInternal(right_page);
    right->hdr.is_leaf    = 0;
    right->hdr.num_keys   = total - mid - 1;
    right->hdr.next_leaf  = INVALID_PAGE_ID;
    right->hdr._pad       = 0;
    for (int32_t i = 0; i < right->hdr.num_keys; ++i)      right->keys[i]     = tmp_k[mid + 1 + i];
    for (int32_t i = 0; i <= right->hdr.num_keys; ++i) right->children[i] = tmp_c[mid + 1 + i];
    bpm_->UnpinPage(right_id, true);   // ← right internal node released here

    return SplitResult{right_id, new_push_key};
}

// ─── Insert ──────────────────────────────────────────────────────────────────
// Traversal keeps every ancestor pinned (stored in `path`).  After the leaf
// operation, pins are released bottom-up as splits propagate.  Every page
// that was pinned is unpinned exactly once before Insert returns.

void BPlusTree::Insert(int64_t key, int64_t value) {
    struct PathEntry { page_id_t page_id; Page *page; int32_t child_idx; };
    std::vector<PathEntry> path;
    path.reserve(16);  // height stays well under 16 for any realistic dataset

    page_id_t cur_id = root_page_id_;
    Page     *cur    = bpm_->FetchPage(cur_id);
    if (!cur) throw std::runtime_error("Insert: cannot fetch root");

    // Descend to the correct leaf, keeping every internal node pinned.
    while (!AsLeaf(cur)->hdr.is_leaf) {
        InternalNode *n = AsInternal(cur);
        int32_t c = static_cast<int32_t>(
            std::upper_bound(n->keys, n->keys + n->hdr.num_keys, key) - n->keys);
        path.push_back({cur_id, cur, c});    // parent stays pinned in path
        page_id_t next_id = n->children[c];
        cur = bpm_->FetchPage(next_id);
        if (!cur) {
            // Unpin everything we've accumulated before throwing.
            for (auto &e : path) bpm_->UnpinPage(e.page_id, false);
            throw std::runtime_error("Insert: cannot fetch child");
        }
        cur_id = next_id;
    }

    // cur / cur_id is the target leaf (pinned).
    auto split = InsertIntoLeaf(cur, key, value);
    bpm_->UnpinPage(cur_id, true);   // leaf always marked dirty

    if (!split) {
        // No split: release all ancestor pins (they were not modified).
        for (auto &e : path) bpm_->UnpinPage(e.page_id, false);
        return;
    }

    // Propagate split bottom-up through the path.
    page_id_t right_id = split->new_page;
    int64_t   up_key   = split->push_key;

    while (!path.empty()) {
        PathEntry pe = path.back();
        path.pop_back();
        // pe.page is still pinned from the traversal above.
        auto split2 = InsertIntoInternal(pe.page, pe.child_idx, up_key, right_id);
        bpm_->UnpinPage(pe.page_id, true);   // internal node was modified

        if (!split2) {
            for (auto &e : path) bpm_->UnpinPage(e.page_id, false);
            return;
        }
        right_id = split2->new_page;
        up_key   = split2->push_key;
    }

    // Root split — create a new root above the old one.
    page_id_t new_root_id;
    Page     *new_root_page = bpm_->NewPage(new_root_id);
    if (!new_root_page) throw std::runtime_error("Insert: cannot allocate new root");
    InternalNode *nr   = AsInternal(new_root_page);
    nr->hdr.is_leaf    = 0;
    nr->hdr.num_keys   = 1;
    nr->hdr.next_leaf  = INVALID_PAGE_ID;
    nr->hdr._pad       = 0;
    nr->keys[0]        = up_key;
    nr->children[0]    = root_page_id_;
    nr->children[1]    = right_id;
    bpm_->UnpinPage(new_root_id, true);
    root_page_id_ = new_root_id;
}

// ─── Delete ──────────────────────────────────────────────────────────────────
// Simple deletion (no rebalancing).  Releases parent pins immediately while
// descending, so only one page is ever pinned at a time.

void BPlusTree::Delete(int64_t key) {
    page_id_t cur_id = root_page_id_;
    Page     *cur    = bpm_->FetchPage(cur_id);
    if (!cur) return;

    while (!AsLeaf(cur)->hdr.is_leaf) {
        InternalNode *n = AsInternal(cur);
        int32_t c = static_cast<int32_t>(
            std::upper_bound(n->keys, n->keys + n->hdr.num_keys, key) - n->keys);
        page_id_t next_id = n->children[c];
        bpm_->UnpinPage(cur_id, false);
        cur = bpm_->FetchPage(next_id);
        if (!cur) return;
        cur_id = next_id;
    }

    LeafNode *leaf = AsLeaf(cur);
    int32_t n   = leaf->hdr.num_keys;
    int32_t pos = static_cast<int32_t>(
        std::lower_bound(leaf->keys, leaf->keys + n, key) - leaf->keys);

    bool dirty = false;
    if (pos < n && leaf->keys[pos] == key) {
        for (int32_t i = pos; i < n - 1; ++i) {
            leaf->keys[i]   = leaf->keys[i + 1];
            leaf->values[i] = leaf->values[i + 1];
        }
        leaf->hdr.num_keys = n - 1;
        dirty = true;
    }
    bpm_->UnpinPage(cur_id, dirty);
}
