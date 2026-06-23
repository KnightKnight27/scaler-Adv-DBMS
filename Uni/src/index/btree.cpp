#include "index/btree.h"
#include <iostream>
#include <algorithm>

BPlusTree::BPlusTree(PageId_t root_page_id, BufferPoolManager* bpm)
    : root_page_id_(root_page_id), bpm_(bpm) {
    if (root_page_id_ == INVALID_PAGE_ID) {
        Page* root_page = bpm_->NewPage(root_page_id_);
        if (root_page) {
            root_page->Init(root_page_id_, PageType::INDEX_PAGE);
            BLeafNode* leaf = reinterpret_cast<BLeafNode*>(root_page->data);
            leaf->header.is_leaf = true;
            leaf->header.num_keys = 0;
            leaf->header.next_page_id = INVALID_PAGE_ID;
            bpm_->UnpinPage(root_page_id_, true);
        }
    }
}

PageId_t BPlusTree::FindLeafPage(int32_t key, std::vector<PageId_t>& path) {
    PageId_t curr_id = root_page_id_;
    while (true) {
        Page* page = bpm_->FetchPage(curr_id);
        if (!page) {
            return INVALID_PAGE_ID;
        }

        BTreeHeader* hdr = reinterpret_cast<BTreeHeader*>(page->data);
        if (hdr->is_leaf) {
            bpm_->UnpinPage(curr_id, false);
            return curr_id;
        }

        path.push_back(curr_id);
        BInternalNode* internal = reinterpret_cast<BInternalNode*>(page->data);
        int num_keys = internal->header.num_keys;
        
        // Find first key > search_key
        int idx = 0;
        while (idx < num_keys && key >= internal->keys[idx]) {
            idx++;
        }
        
        PageId_t next_id = internal->children[idx];
        bpm_->UnpinPage(curr_id, false);
        curr_id = next_id;
    }
}

bool BPlusTree::Search(int32_t key, RID& result) {
    std::vector<PageId_t> path;
    PageId_t leaf_id = FindLeafPage(key, path);
    if (leaf_id == INVALID_PAGE_ID) {
        return false;
    }

    Page* page = bpm_->FetchPage(leaf_id);
    BLeafNode* leaf = reinterpret_cast<BLeafNode*>(page->data);
    int num_keys = leaf->header.num_keys;

    auto it = std::lower_bound(leaf->keys, leaf->keys + num_keys, key);
    int idx = std::distance(leaf->keys, it);

    bool found = false;
    if (idx < num_keys && leaf->keys[idx] == key) {
        result = leaf->values[idx];
        found = true;
    }

    bpm_->UnpinPage(leaf_id, false);
    return found;
}

bool BPlusTree::Insert(int32_t key, const RID& value) {
    std::vector<PageId_t> path;
    PageId_t leaf_id = FindLeafPage(key, path);
    if (leaf_id == INVALID_PAGE_ID) {
        return false;
    }

    Page* page = bpm_->FetchPage(leaf_id);
    BLeafNode* leaf = reinterpret_cast<BLeafNode*>(page->data);
    int num_keys = leaf->header.num_keys;

    // Search if key already exists
    auto it = std::lower_bound(leaf->keys, leaf->keys + num_keys, key);
    int idx = std::distance(leaf->keys, it);
    if (idx < num_keys && leaf->keys[idx] == key) {
        // Duplicate key, reject for unique PK index
        bpm_->UnpinPage(leaf_id, false);
        return false;
    }

    // Shift and insert
    for (int i = num_keys; i > idx; --i) {
        leaf->keys[i] = leaf->keys[i - 1];
        leaf->values[i] = leaf->values[i - 1];
    }
    leaf->keys[idx] = key;
    leaf->values[idx] = value;
    leaf->header.num_keys++;

    if (leaf->header.num_keys < BTREE_MAX_KEYS) {
        bpm_->UnpinPage(leaf_id, true);
        return true;
    }

    // Split needed
    bpm_->UnpinPage(leaf_id, true); // Save changes before split
    SplitLeaf(leaf_id, path);
    return true;
}

void BPlusTree::SplitLeaf(PageId_t leaf_page_id, std::vector<PageId_t>& path) {
    Page* leaf_page = bpm_->FetchPage(leaf_page_id);
    BLeafNode* leaf = reinterpret_cast<BLeafNode*>(leaf_page->data);

    PageId_t new_leaf_id;
    Page* new_leaf_page = bpm_->NewPage(new_leaf_id);
    new_leaf_page->Init(new_leaf_id, PageType::INDEX_PAGE);
    BLeafNode* new_leaf = reinterpret_cast<BLeafNode*>(new_leaf_page->data);
    new_leaf->header.is_leaf = true;
    new_leaf->header.num_keys = 0;

    int split_idx = BTREE_MAX_KEYS / 2;
    int move_count = BTREE_MAX_KEYS - split_idx;

    for (int i = 0; i < move_count; ++i) {
        new_leaf->keys[i] = leaf->keys[split_idx + i];
        new_leaf->values[i] = leaf->values[split_idx + i];
    }

    new_leaf->header.num_keys = move_count;
    leaf->header.num_keys = split_idx;

    new_leaf->header.next_page_id = leaf->header.next_page_id;
    leaf->header.next_page_id = new_leaf_id;

    int32_t split_key = new_leaf->keys[0];

    bpm_->UnpinPage(leaf_page_id, true);
    bpm_->UnpinPage(new_leaf_id, true);

    InsertIntoParent(leaf_page_id, split_key, new_leaf_id, path);
}

void BPlusTree::InsertIntoParent(PageId_t child_id, int32_t key, PageId_t new_child_id, std::vector<PageId_t>& path) {
    if (path.empty()) {
        // Create new root
        PageId_t new_root_id;
        Page* new_root_page = bpm_->NewPage(new_root_id);
        new_root_page->Init(new_root_id, PageType::INDEX_PAGE);
        BInternalNode* new_root = reinterpret_cast<BInternalNode*>(new_root_page->data);

        new_root->header.is_leaf = false;
        new_root->header.num_keys = 1;
        new_root->keys[0] = key;
        new_root->children[0] = child_id;
        new_root->children[1] = new_child_id;

        root_page_id_ = new_root_id;
        bpm_->UnpinPage(new_root_id, true);
        return;
    }

    PageId_t parent_id = path.back();
    path.pop_back();

    Page* parent_page = bpm_->FetchPage(parent_id);
    BInternalNode* parent = reinterpret_cast<BInternalNode*>(parent_page->data);
    int num_keys = parent->header.num_keys;

    // Find insertion index
    int idx = 0;
    while (idx < num_keys && key >= parent->keys[idx]) {
        idx++;
    }

    // Shift keys and children
    for (int i = num_keys; i > idx; --i) {
        parent->keys[i] = parent->keys[i - 1];
        parent->children[i + 1] = parent->children[i];
    }
    parent->keys[idx] = key;
    parent->children[idx + 1] = new_child_id;
    parent->header.num_keys++;

    if (parent->header.num_keys < BTREE_MAX_KEYS) {
        bpm_->UnpinPage(parent_id, true);
        return;
    }

    // Parent overflow, split parent internal node
    bpm_->UnpinPage(parent_id, true);
    SplitInternal(parent_id, path);
}

void BPlusTree::SplitInternal(PageId_t internal_page_id, std::vector<PageId_t>& path) {
    Page* parent_page = bpm_->FetchPage(internal_page_id);
    BInternalNode* parent = reinterpret_cast<BInternalNode*>(parent_page->data);

    PageId_t new_parent_id;
    Page* new_parent_page = bpm_->NewPage(new_parent_id);
    new_parent_page->Init(new_parent_id, PageType::INDEX_PAGE);
    BInternalNode* new_parent = reinterpret_cast<BInternalNode*>(new_parent_page->data);
    new_parent->header.is_leaf = false;

    int split_idx = BTREE_MAX_KEYS / 2; // e.g. 100
    int32_t push_up_key = parent->keys[split_idx];

    // Move keys and children to the new internal node
    // Key at split_idx is pushed up, so new node keys start from split_idx + 1
    int move_count = BTREE_MAX_KEYS - split_idx - 1;
    for (int i = 0; i < move_count; ++i) {
        new_parent->keys[i] = parent->keys[split_idx + 1 + i];
    }
    for (int i = 0; i <= move_count; ++i) {
        new_parent->children[i] = parent->children[split_idx + 1 + i];
    }

    new_parent->header.num_keys = move_count;
    parent->header.num_keys = split_idx;

    bpm_->UnpinPage(internal_page_id, true);
    bpm_->UnpinPage(new_parent_id, true);

    InsertIntoParent(internal_page_id, push_up_key, new_parent_id, path);
}

bool BPlusTree::Delete(int32_t key) {
    std::vector<PageId_t> path;
    PageId_t leaf_id = FindLeafPage(key, path);
    if (leaf_id == INVALID_PAGE_ID) {
        return false;
    }

    Page* page = bpm_->FetchPage(leaf_id);
    BLeafNode* leaf = reinterpret_cast<BLeafNode*>(page->data);
    int num_keys = leaf->header.num_keys;

    auto it = std::lower_bound(leaf->keys, leaf->keys + num_keys, key);
    int idx = std::distance(leaf->keys, it);

    if (idx < num_keys && leaf->keys[idx] == key) {
        // Shift keys/values left
        for (int i = idx; i < num_keys - 1; ++i) {
            leaf->keys[i] = leaf->keys[i + 1];
            leaf->values[i] = leaf->values[i + 1];
        }
        leaf->header.num_keys--;
        bpm_->UnpinPage(leaf_id, true);
        return true;
    }

    bpm_->UnpinPage(leaf_id, false);
    return false;
}

void BPlusTree::PrintTree() {
    std::cout << "--- B+ Tree Structure (Root: " << root_page_id_ << ") ---" << std::endl;
    PrintNode(root_page_id_, 0);
    std::cout << "---------------------------------------------" << std::endl;
}

void BPlusTree::PrintNode(PageId_t page_id, int level) {
    if (page_id == INVALID_PAGE_ID) return;
    Page* page = bpm_->FetchPage(page_id);
    if (!page) return;

    BTreeHeader* hdr = reinterpret_cast<BTreeHeader*>(page->data);
    std::string indent(level * 4, ' ');

    if (hdr->is_leaf) {
        BLeafNode* leaf = reinterpret_cast<BLeafNode*>(page->data);
        std::cout << indent << "Leaf Page: " << page_id << " (Keys: " << leaf->header.num_keys << ", Next: " << leaf->header.next_page_id << ") [";
        for (int i = 0; i < leaf->header.num_keys; ++i) {
            std::cout << leaf->keys[i] << "->(" << leaf->values[i].page_id << "," << leaf->values[i].slot_id << ")";
            if (i < leaf->header.num_keys - 1) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    } else {
        BInternalNode* internal = reinterpret_cast<BInternalNode*>(page->data);
        std::cout << indent << "Internal Page: " << page_id << " (Keys: " << internal->header.num_keys << ") [";
        for (int i = 0; i < internal->header.num_keys; ++i) {
            std::cout << internal->keys[i];
            if (i < internal->header.num_keys - 1) std::cout << ", ";
        }
        std::cout << "]" << std::endl;

        std::vector<PageId_t> children_to_print;
        for (int i = 0; i <= internal->header.num_keys; ++i) {
            children_to_print.push_back(internal->children[i]);
        }
        bpm_->UnpinPage(page_id, false);

        for (PageId_t child : children_to_print) {
            PrintNode(child, level + 1);
        }
        return; // Already unpinned
    }

    bpm_->UnpinPage(page_id, false);
}
