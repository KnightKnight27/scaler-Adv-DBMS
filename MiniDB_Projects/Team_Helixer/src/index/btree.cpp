#include "index/btree.h"
#include <algorithm>

namespace minidb {

BPlusTree::BPlusTree(BufferPoolManager *bpm) : bpm_(bpm) {}

// Descend from root to the leaf that should hold `key`. Each visited *internal*
// node id is pushed onto *path so a subsequent split can walk back up.
page_id_t BPlusTree::find_leaf(int32_t key, std::vector<page_id_t> *path) {
    page_id_t cur = root_page_id_;
    while (true) {
        Page *pg = bpm_->fetch_page(cur);
        BTreeNode *node = as_node(pg);
        if (node->is_leaf) {
            bpm_->unpin_page(cur, false);
            return cur;
        }
        if (path) path->push_back(cur);
        // Find first key strictly greater than `key`; descend that child.
        int i = 0;
        while (i < node->size && key >= node->keys[i]) ++i;
        page_id_t child = node->children[i];
        bpm_->unpin_page(cur, false);
        cur = child;
    }
}

bool BPlusTree::search(int32_t key, RID *out_rid) {
    if (root_page_id_ == INVALID_PAGE_ID) return false;
    page_id_t leaf_id = find_leaf(key, nullptr);
    Page *pg = bpm_->fetch_page(leaf_id);
    BTreeNode *leaf = as_node(pg);
    bool found = false;
    for (int i = 0; i < leaf->size; ++i) {
        if (leaf->keys[i] == key) {
            if (out_rid) *out_rid = leaf->rids[i];
            found = true;
            break;
        }
    }
    bpm_->unpin_page(leaf_id, false);
    return found;
}

bool BPlusTree::insert(int32_t key, const RID &rid) {
    // First insert ever: create a leaf root.
    if (root_page_id_ == INVALID_PAGE_ID) {
        page_id_t pid;
        Page *pg = bpm_->new_page(&pid);
        BTreeNode *leaf = as_node(pg);
        leaf->is_leaf = 1;
        leaf->size = 1;
        leaf->next = INVALID_PAGE_ID;
        leaf->self = pid;
        leaf->keys[0] = key;
        leaf->rids[0] = rid;
        bpm_->unpin_page(pid, true);
        root_page_id_ = pid;
        return true;
    }

    std::vector<page_id_t> path;
    page_id_t leaf_id = find_leaf(key, &path);
    Page *pg = bpm_->fetch_page(leaf_id);
    BTreeNode *leaf = as_node(pg);

    // Locate insert position; reject duplicate keys (primary-key semantics).
    int pos = 0;
    while (pos < leaf->size && leaf->keys[pos] < key) ++pos;
    if (pos < leaf->size && leaf->keys[pos] == key) {
        bpm_->unpin_page(leaf_id, false);
        return false;
    }

    // Fast path: room in the leaf, just shift and insert.
    if (leaf->size < BTREE_ORDER) {
        for (int i = leaf->size; i > pos; --i) {
            leaf->keys[i] = leaf->keys[i - 1];
            leaf->rids[i] = leaf->rids[i - 1];
        }
        leaf->keys[pos] = key;
        leaf->rids[pos] = rid;
        leaf->size++;
        bpm_->unpin_page(leaf_id, true);
        return true;
    }

    // Slow path: leaf is full. Build the over-full sequence then split in two.
    std::vector<int32_t> tk(BTREE_ORDER + 1);
    std::vector<RID>     tr(BTREE_ORDER + 1);
    for (int i = 0; i < pos; ++i) { tk[i] = leaf->keys[i]; tr[i] = leaf->rids[i]; }
    tk[pos] = key; tr[pos] = rid;
    for (int i = pos; i < BTREE_ORDER; ++i) { tk[i + 1] = leaf->keys[i]; tr[i + 1] = leaf->rids[i]; }

    int total = BTREE_ORDER + 1;
    int left_n = (total + 1) / 2;     // left leaf keeps the first ceil(total/2)

    page_id_t new_id;
    Page *npg = bpm_->new_page(&new_id);
    BTreeNode *nleaf = as_node(npg);
    nleaf->is_leaf = 1;
    nleaf->self = new_id;

    // Refill the original (left) leaf.
    leaf->size = left_n;
    for (int i = 0; i < left_n; ++i) { leaf->keys[i] = tk[i]; leaf->rids[i] = tr[i]; }
    // Fill the new (right) leaf.
    nleaf->size = total - left_n;
    for (int i = 0; i < nleaf->size; ++i) { nleaf->keys[i] = tk[left_n + i]; nleaf->rids[i] = tr[left_n + i]; }

    // Splice the new leaf into the linked list of leaves.
    nleaf->next = leaf->next;
    leaf->next = new_id;

    int32_t up_key = nleaf->keys[0]; // separator copied up to the parent
    bpm_->unpin_page(leaf_id, true);
    bpm_->unpin_page(new_id, true);

    insert_into_parent(path, leaf_id, up_key, new_id);
    return true;
}

void BPlusTree::insert_into_parent(std::vector<page_id_t> &path, page_id_t left,
                                   int32_t key, page_id_t right) {
    // The split reached the root: grow the tree by one level.
    if (path.empty()) {
        page_id_t rid;
        Page *pg = bpm_->new_page(&rid);
        BTreeNode *root = as_node(pg);
        root->is_leaf = 0;
        root->size = 1;
        root->next = INVALID_PAGE_ID;
        root->self = rid;
        root->keys[0] = key;
        root->children[0] = left;
        root->children[1] = right;
        bpm_->unpin_page(rid, true);
        root_page_id_ = rid;
        return;
    }

    page_id_t parent_id = path.back();
    path.pop_back();
    Page *pg = bpm_->fetch_page(parent_id);
    BTreeNode *parent = as_node(pg);

    // Find where `left` sits among the parent's children; the new key/child go
    // immediately after it.
    int j = 0;
    while (j <= parent->size && parent->children[j] != left) ++j;

    if (parent->size < BTREE_ORDER) {
        // Room available: shift keys/children right and insert.
        for (int i = parent->size; i > j; --i) parent->keys[i] = parent->keys[i - 1];
        for (int i = parent->size + 1; i > j + 1; --i) parent->children[i] = parent->children[i - 1];
        parent->keys[j] = key;
        parent->children[j + 1] = right;
        parent->size++;
        bpm_->unpin_page(parent_id, true);
        return;
    }

    // Parent full: build the over-full key/child sequences and split.
    std::vector<int32_t>   tk(BTREE_ORDER + 1);
    std::vector<page_id_t> tc(BTREE_ORDER + 2);
    for (int i = 0; i < j; ++i) tk[i] = parent->keys[i];
    tk[j] = key;
    for (int i = j; i < BTREE_ORDER; ++i) tk[i + 1] = parent->keys[i];
    for (int i = 0; i <= j; ++i) tc[i] = parent->children[i];
    tc[j + 1] = right;
    for (int i = j + 1; i <= BTREE_ORDER; ++i) tc[i + 1] = parent->children[i];

    int total = BTREE_ORDER + 1;     // keys in the over-full node
    int mid = total / 2;             // this key moves UP (not copied)
    int32_t up_key = tk[mid];

    page_id_t new_id;
    Page *npg = bpm_->new_page(&new_id);
    BTreeNode *ninternal = as_node(npg);
    ninternal->is_leaf = 0;
    ninternal->next = INVALID_PAGE_ID;
    ninternal->self = new_id;

    // Left (original) parent keeps keys[0,mid) and children[0,mid].
    parent->size = mid;
    for (int i = 0; i < mid; ++i) parent->keys[i] = tk[i];
    for (int i = 0; i <= mid; ++i) parent->children[i] = tc[i];

    // Right (new) internal node gets keys[mid+1,total) and children[mid+1,total+1).
    ninternal->size = total - mid - 1;
    for (int i = 0; i < ninternal->size; ++i) ninternal->keys[i] = tk[mid + 1 + i];
    for (int i = 0; i <= ninternal->size; ++i) ninternal->children[i] = tc[mid + 1 + i];

    bpm_->unpin_page(parent_id, true);
    bpm_->unpin_page(new_id, true);

    insert_into_parent(path, parent_id, up_key, new_id);
}

bool BPlusTree::remove(int32_t key) {
    if (root_page_id_ == INVALID_PAGE_ID) return false;
    page_id_t leaf_id = find_leaf(key, nullptr);
    Page *pg = bpm_->fetch_page(leaf_id);
    BTreeNode *leaf = as_node(pg);

    int pos = -1;
    for (int i = 0; i < leaf->size; ++i) {
        if (leaf->keys[i] == key) { pos = i; break; }
    }
    if (pos < 0) { bpm_->unpin_page(leaf_id, false); return false; }

    // Lazy delete: shift entries left over the removed slot. No merge/rebalance.
    for (int i = pos; i < leaf->size - 1; ++i) {
        leaf->keys[i] = leaf->keys[i + 1];
        leaf->rids[i] = leaf->rids[i + 1];
    }
    leaf->size--;

    // If the tree's only leaf becomes empty, reset to an empty tree.
    if (leaf->size == 0 && leaf_id == root_page_id_) {
        root_page_id_ = INVALID_PAGE_ID;
    }
    bpm_->unpin_page(leaf_id, true);
    return true;
}

std::vector<RID> BPlusTree::range(int32_t low, int32_t high) {
    std::vector<RID> out;
    if (root_page_id_ == INVALID_PAGE_ID) return out;
    page_id_t leaf_id = find_leaf(low, nullptr);
    while (leaf_id != INVALID_PAGE_ID) {
        Page *pg = bpm_->fetch_page(leaf_id);
        BTreeNode *leaf = as_node(pg);
        bool done = false;
        for (int i = 0; i < leaf->size; ++i) {
            if (leaf->keys[i] < low) continue;
            if (leaf->keys[i] > high) { done = true; break; }
            out.push_back(leaf->rids[i]);
        }
        page_id_t nxt = leaf->next;
        bpm_->unpin_page(leaf_id, false);
        if (done) break;
        leaf_id = nxt;
    }
    return out;
}

} // namespace minidb
