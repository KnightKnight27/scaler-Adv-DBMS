#include "index/bplus_tree.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>

// ============================================================
// Constructor / Destructor
// ============================================================

BPlusTree::BPlusTree(const std::string& index_file) {
    disk_mgr_ = new DiskManager(index_file);
    pool_ = new BufferPool(disk_mgr_, 16);

    if (disk_mgr_->GetNumPages() == 0) {
        // New tree: create metadata page (page 0) + empty root leaf (page 1)
        int meta_id;
        pool_->NewPage(meta_id); // page 0 = metadata
        pool_->UnpinPage(meta_id, true);

        root_page_id_ = CreateNode(NODE_LEAF);
        WriteMetadata();
    } else {
        ReadMetadata();
    }
}

BPlusTree::~BPlusTree() {
    WriteMetadata();
    pool_->FlushAll();
    delete pool_;
    delete disk_mgr_;
}

bool BPlusTree::IsEmpty() const {
    return root_page_id_ == INVALID_PAGE_ID;
}

// ============================================================
// Metadata page (page 0): [root_page_id(4B)]
// ============================================================

void BPlusTree::WriteMetadata() {
    Page* meta = pool_->FetchPage(0);
    char* data = meta->GetData();
    memcpy(data, &root_page_id_, 4);
    pool_->UnpinPage(0, true);
}

void BPlusTree::ReadMetadata() {
    Page* meta = pool_->FetchPage(0);
    memcpy(&root_page_id_, meta->GetData(), 4);
    pool_->UnpinPage(0, false);
}

// ============================================================
// Node creation
// ============================================================

int BPlusTree::CreateNode(uint8_t node_type) {
    int page_id;
    Page* page = pool_->NewPage(page_id);
    memset(page->GetData(), 0, PAGE_SIZE);
    SetNodeType(page, node_type);
    SetNumKeys(page, 0);
    SetNextLeaf(page, INVALID_PAGE_ID);
    pool_->UnpinPage(page_id, true);
    return page_id;
}

// ============================================================
// Node field accessors — read/write directly to page data
// ============================================================

uint8_t BPlusTree::GetNodeType(Page* p) { return (uint8_t)p->GetData()[0]; }
void BPlusTree::SetNodeType(Page* p, uint8_t t) { p->GetData()[0] = t; }

uint16_t BPlusTree::GetNumKeys(Page* p) {
    uint16_t n; memcpy(&n, p->GetData() + 1, 2); return n;
}
void BPlusTree::SetNumKeys(Page* p, uint16_t n) { memcpy(p->GetData() + 1, &n, 2); }

int BPlusTree::GetNextLeaf(Page* p) {
    int v; memcpy(&v, p->GetData() + 3, 4); return v;
}
void BPlusTree::SetNextLeaf(Page* p, int next) { memcpy(p->GetData() + 3, &next, 4); }

int BPlusTree::GetKey(Page* p, int idx) {
    int k; memcpy(&k, p->GetData() + KEYS_OFFSET + idx * 4, 4); return k;
}
void BPlusTree::SetKey(Page* p, int idx, int key) {
    memcpy(p->GetData() + KEYS_OFFSET + idx * 4, &key, 4);
}

int BPlusTree::GetChild(Page* p, int idx) {
    int c; memcpy(&c, p->GetData() + INTERNAL_CHILDREN_OFFSET + idx * 4, 4); return c;
}
void BPlusTree::SetChild(Page* p, int idx, int child_page_id) {
    memcpy(p->GetData() + INTERNAL_CHILDREN_OFFSET + idx * 4, &child_page_id, 4);
}

RID BPlusTree::GetLeafRID(Page* p, int idx) {
    RID rid;
    int off = LEAF_RIDS_OFFSET + idx * 6;
    memcpy(&rid.page_id, p->GetData() + off, 4);
    memcpy(&rid.slot_id, p->GetData() + off + 4, 2);
    return rid;
}
void BPlusTree::SetLeafRID(Page* p, int idx, const RID& rid) {
    int off = LEAF_RIDS_OFFSET + idx * 6;
    memcpy(p->GetData() + off, &rid.page_id, 4);
    memcpy(p->GetData() + off + 4, &rid.slot_id, 2);
}

// ============================================================
// FindLeaf — traverse from root to the leaf that should contain key
// ============================================================

int BPlusTree::FindLeaf(int key, std::vector<int>& path) {
    int current = root_page_id_;
    while (true) {
        Page* page = pool_->FetchPage(current);
        if (GetNodeType(page) == NODE_LEAF) {
            pool_->UnpinPage(current, false);
            return current;
        }
        // Internal node: find which child to descend into
        int n = GetNumKeys(page);
        int i = 0;
        while (i < n && key >= GetKey(page, i)) i++;
        path.push_back(current);
        int child = GetChild(page, i);
        pool_->UnpinPage(current, false);
        current = child;
    }
}

// ============================================================
// Search — exact key lookup
// ============================================================

std::optional<RID> BPlusTree::Search(int key) {
    std::vector<int> path;
    int leaf_id = FindLeaf(key, path);

    Page* leaf = pool_->FetchPage(leaf_id);
    int n = GetNumKeys(leaf);
    for (int i = 0; i < n; i++) {
        if (GetKey(leaf, i) == key) {
            RID rid = GetLeafRID(leaf, i);
            pool_->UnpinPage(leaf_id, false);
            return rid;
        }
    }
    pool_->UnpinPage(leaf_id, false);
    return std::nullopt;
}

// ============================================================
// RangeScan — walk leaf chain from lowKey to highKey
// ============================================================

std::vector<RID> BPlusTree::RangeScan(int low_key, int high_key) {
    std::vector<RID> results;
    std::vector<int> path;
    int leaf_id = FindLeaf(low_key, path);

    while (leaf_id != INVALID_PAGE_ID) {
        Page* leaf = pool_->FetchPage(leaf_id);
        int n = GetNumKeys(leaf);
        bool done = false;
        for (int i = 0; i < n; i++) {
            int k = GetKey(leaf, i);
            if (k > high_key) { done = true; break; }
            if (k >= low_key) {
                results.push_back(GetLeafRID(leaf, i));
            }
        }
        int next = GetNextLeaf(leaf);
        pool_->UnpinPage(leaf_id, false);
        if (done) break;
        leaf_id = next;
    }
    return results;
}

// ============================================================
// Insert
// ============================================================

void BPlusTree::Insert(int key, const RID& rid) {
    std::vector<int> path;
    int leaf_id = FindLeaf(key, path);

    Page* leaf = pool_->FetchPage(leaf_id);
    int n = GetNumKeys(leaf);

    if (n < LEAF_MAX_KEYS) {
        // Room in the leaf — insert directly
        InsertIntoLeaf(leaf, key, rid);
        pool_->UnpinPage(leaf_id, true);
    } else {
        // Leaf is full — need to split
        pool_->UnpinPage(leaf_id, false);
        SplitLeaf(leaf_id, key, rid, path);
    }
}

void BPlusTree::InsertIntoLeaf(Page* leaf, int key, const RID& rid) {
    int n = GetNumKeys(leaf);
    // Find insertion position (sorted order)
    int i = 0;
    while (i < n && GetKey(leaf, i) < key) i++;

    // Shift entries right
    for (int j = n; j > i; j--) {
        SetKey(leaf, j, GetKey(leaf, j - 1));
        SetLeafRID(leaf, j, GetLeafRID(leaf, j - 1));
    }
    SetKey(leaf, i, key);
    SetLeafRID(leaf, i, rid);
    SetNumKeys(leaf, n + 1);
}

// ============================================================
// SplitLeaf — leaf is full, split into two and push key up
// ============================================================

void BPlusTree::SplitLeaf(int leaf_id, int key, const RID& rid, std::vector<int>& path) {
    Page* old_leaf = pool_->FetchPage(leaf_id);
    int n = GetNumKeys(old_leaf);

    // Collect all entries + new one into temp arrays
    std::vector<int> all_keys(n + 1);
    std::vector<RID> all_rids(n + 1);
    int insert_pos = 0;
    while (insert_pos < n && GetKey(old_leaf, insert_pos) < key) insert_pos++;

    for (int i = 0, j = 0; j < n + 1; j++) {
        if (j == insert_pos) {
            all_keys[j] = key;
            all_rids[j] = rid;
        } else {
            all_keys[j] = GetKey(old_leaf, i);
            all_rids[j] = GetLeafRID(old_leaf, i);
            i++;
        }
    }

    // Create new leaf
    int new_leaf_id = CreateNode(NODE_LEAF);
    Page* new_leaf = pool_->FetchPage(new_leaf_id);

    // Split point: first half stays, second half moves
    int split = (n + 1) / 2;

    // Write first half back to old leaf
    SetNumKeys(old_leaf, split);
    for (int i = 0; i < split; i++) {
        SetKey(old_leaf, i, all_keys[i]);
        SetLeafRID(old_leaf, i, all_rids[i]);
    }

    // Write second half to new leaf
    int new_count = (n + 1) - split;
    SetNumKeys(new_leaf, new_count);
    for (int i = 0; i < new_count; i++) {
        SetKey(new_leaf, i, all_keys[split + i]);
        SetLeafRID(new_leaf, i, all_rids[split + i]);
    }

    // Update leaf chain: old → new → old's old next
    SetNextLeaf(new_leaf, GetNextLeaf(old_leaf));
    SetNextLeaf(old_leaf, new_leaf_id);

    // The key to push up = first key of the new leaf
    int push_up_key = GetKey(new_leaf, 0);

    pool_->UnpinPage(leaf_id, true);
    pool_->UnpinPage(new_leaf_id, true);

    // Insert the pushed-up key into the parent
    InsertIntoParent(leaf_id, push_up_key, new_leaf_id, path);
}

// ============================================================
// InsertIntoParent — after a split, push a key up to the parent
// ============================================================

void BPlusTree::InsertIntoParent(int left_id, int key, int right_id, std::vector<int>& path) {
    // If the left node was the root, create a new root
    if (left_id == root_page_id_ && path.empty()) {
        int new_root_id = CreateNode(NODE_INTERNAL);
        Page* new_root = pool_->FetchPage(new_root_id);
        SetNumKeys(new_root, 1);
        SetKey(new_root, 0, key);
        SetChild(new_root, 0, left_id);
        SetChild(new_root, 1, right_id);
        pool_->UnpinPage(new_root_id, true);
        root_page_id_ = new_root_id;
        WriteMetadata();
        return;
    }

    // Otherwise, insert into the parent internal node
    int parent_id = path.back();
    path.pop_back();

    Page* parent = pool_->FetchPage(parent_id);
    int n = GetNumKeys(parent);

    if (n < INTERNAL_MAX_KEYS) {
        // Room — insert key + right child
        int i = 0;
        while (i < n && GetKey(parent, i) < key) i++;

        // Shift keys and children right
        for (int j = n; j > i; j--) {
            SetKey(parent, j, GetKey(parent, j - 1));
            SetChild(parent, j + 1, GetChild(parent, j));
        }
        SetKey(parent, i, key);
        SetChild(parent, i + 1, right_id);
        SetNumKeys(parent, n + 1);
        pool_->UnpinPage(parent_id, true);
    } else {
        // Parent is full — split it too
        pool_->UnpinPage(parent_id, false);
        SplitInternal(parent_id, key, right_id, path);
    }
}

// ============================================================
// SplitInternal — internal node is full, split and push middle up
// ============================================================

void BPlusTree::SplitInternal(int node_id, int key, int right_child, std::vector<int>& path) {
    Page* node = pool_->FetchPage(node_id);
    int n = GetNumKeys(node);

    // Gather all keys + new key, all children + new child
    std::vector<int> all_keys(n + 1);
    std::vector<int> all_children(n + 2);

    int insert_pos = 0;
    while (insert_pos < n && GetKey(node, insert_pos) < key) insert_pos++;

    for (int i = 0, j = 0; j < n + 1; j++) {
        if (j == insert_pos) {
            all_keys[j] = key;
        } else {
            all_keys[j] = GetKey(node, i);
            i++;
        }
    }
    for (int i = 0, j = 0; j < n + 2; j++) {
        if (j == insert_pos + 1) {
            all_children[j] = right_child;
        } else {
            all_children[j] = GetChild(node, i);
            i++;
        }
    }

    // Split: middle key goes up, left half stays, right half to new node
    int mid = (n + 1) / 2;  // index of the key that goes up
    int push_up_key = all_keys[mid];

    // Left node keeps keys[0..mid-1] and children[0..mid]
    SetNumKeys(node, mid);
    for (int i = 0; i < mid; i++) {
        SetKey(node, i, all_keys[i]);
    }
    for (int i = 0; i <= mid; i++) {
        SetChild(node, i, all_children[i]);
    }

    // Right node gets keys[mid+1..n] and children[mid+1..n+1]
    int new_node_id = CreateNode(NODE_INTERNAL);
    Page* new_node = pool_->FetchPage(new_node_id);
    int right_count = n - mid;  // n+1 total keys, minus mid+1 (left + pushed up) = n - mid
    SetNumKeys(new_node, right_count);
    for (int i = 0; i < right_count; i++) {
        SetKey(new_node, i, all_keys[mid + 1 + i]);
    }
    for (int i = 0; i <= right_count; i++) {
        SetChild(new_node, i, all_children[mid + 1 + i]);
    }

    pool_->UnpinPage(node_id, true);
    pool_->UnpinPage(new_node_id, true);

    InsertIntoParent(node_id, push_up_key, new_node_id, path);
}

// ============================================================
// Delete — lazy deletion (remove from leaf, no rebalancing)
// ============================================================

bool BPlusTree::Delete(int key) {
    std::vector<int> path;
    int leaf_id = FindLeaf(key, path);

    Page* leaf = pool_->FetchPage(leaf_id);
    int n = GetNumKeys(leaf);

    // Find the key
    int pos = -1;
    for (int i = 0; i < n; i++) {
        if (GetKey(leaf, i) == key) { pos = i; break; }
    }

    if (pos == -1) {
        pool_->UnpinPage(leaf_id, false);
        return false;  // key not found
    }

    // Shift entries left to fill the gap
    for (int i = pos; i < n - 1; i++) {
        SetKey(leaf, i, GetKey(leaf, i + 1));
        SetLeafRID(leaf, i, GetLeafRID(leaf, i + 1));
    }
    SetNumKeys(leaf, n - 1);

    pool_->UnpinPage(leaf_id, true);
    return true;
}
