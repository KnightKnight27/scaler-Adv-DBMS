#include "index/b_plus_tree.h"
#include <iostream>

namespace minidb {

BPlusTree::BPlusTree(BufferPool* buffer_pool, PageId meta_page_id)
    : buffer_pool_(buffer_pool), meta_page_id_(meta_page_id) {
    
    Page* meta_page = buffer_pool_->FetchPage(meta_page_id_);
    auto* meta = reinterpret_cast<MetaPage*>(meta_page->data_);
    if (meta->magic_ == META_MAGIC) {
        root_page_id_ = meta->root_page_id_;
    } else {
        root_page_id_ = INVALID_PAGE_ID;
    }
    buffer_pool_->UnpinPage(meta_page_id_, false);
}

void BPlusTree::UpdateRootId(PageId new_root) {
    root_page_id_ = new_root;
    Page* meta_page = buffer_pool_->FetchPage(meta_page_id_);
    auto* meta = reinterpret_cast<MetaPage*>(meta_page->data_);
    meta->root_page_id_ = root_page_id_;
    meta->magic_ = META_MAGIC;
    buffer_pool_->UnpinPage(meta_page_id_, true);
}

bool BPlusTree::Insert(int32_t key, const RecordId& value) {
    std::lock_guard<std::mutex> lock(root_latch_);
    if (root_page_id_ == INVALID_PAGE_ID) {
        StartNewTree(key, value);
        return true;
    }
    return InsertIntoLeaf(key, value);
}

void BPlusTree::StartNewTree(int32_t key, const RecordId& value) {
    PageId new_root_id;
    Page* root_page = buffer_pool_->NewPage(&new_root_id);
    auto* root_node = reinterpret_cast<BPlusLeafNode*>(root_page->data_);
    root_node->Init(new_root_id, INVALID_PAGE_ID, 
        (PAGE_SIZE - sizeof(BPlusNodeHeader) - sizeof(PageId)) / (sizeof(int32_t) + sizeof(RecordId)) - 1);
    
    root_node->keys_[0] = key;
    root_node->values_[0] = value;
    root_node->header_.size_ = 1;
    
    UpdateRootId(new_root_id);
    
    buffer_pool_->UnpinPage(new_root_id, true);
}

PageId BPlusTree::FindLeafPage(int32_t key, bool exclusive_lock) {
    (void)exclusive_lock;
    if (root_page_id_ == INVALID_PAGE_ID) return INVALID_PAGE_ID;
    
    PageId current_page_id = root_page_id_;
    Page* current_page = buffer_pool_->FetchPage(current_page_id);
    auto* node = reinterpret_cast<BPlusNodeHeader*>(current_page->data_);
    
    while (node->type_ == IndexNodeType::INTERNAL) {
        auto* internal_node = reinterpret_cast<BPlusInternalNode*>(current_page->data_);
        
        // Find the child pointer
        int idx = 0;
        while (idx < internal_node->header_.size_ && key >= internal_node->keys_[idx]) {
            idx++;
        }
        
        PageId next_page_id = internal_node->values_[idx];
        buffer_pool_->UnpinPage(current_page_id, false);
        
        current_page_id = next_page_id;
        current_page = buffer_pool_->FetchPage(current_page_id);
        node = reinterpret_cast<BPlusNodeHeader*>(current_page->data_);
    }
    
    buffer_pool_->UnpinPage(current_page_id, false);
    return current_page_id;
}

bool BPlusTree::InsertIntoLeaf(int32_t key, const RecordId& value) {
    PageId leaf_page_id = FindLeafPage(key);
    Page* leaf_page = buffer_pool_->FetchPage(leaf_page_id);
    auto* leaf_node = reinterpret_cast<BPlusLeafNode*>(leaf_page->data_);
    
    // Check if key exists (assume unique keys)
    for (int i = 0; i < leaf_node->header_.size_; i++) {
        if (leaf_node->keys_[i] == key) {
            buffer_pool_->UnpinPage(leaf_page_id, false);
            return false;
        }
    }
    
    // Insert in sorted order
    int insert_idx = 0;
    while (insert_idx < leaf_node->header_.size_ && key > leaf_node->keys_[insert_idx]) {
        insert_idx++;
    }
    
    for (int i = leaf_node->header_.size_; i > insert_idx; i--) {
        leaf_node->keys_[i] = leaf_node->keys_[i - 1];
        leaf_node->values_[i] = leaf_node->values_[i - 1];
    }
    
    leaf_node->keys_[insert_idx] = key;
    leaf_node->values_[insert_idx] = value;
    leaf_node->header_.size_++;
    
    if (leaf_node->header_.size_ > leaf_node->header_.max_size_) {
        SplitLeaf(leaf_node, leaf_page_id);
    }
    
    buffer_pool_->UnpinPage(leaf_page_id, true);
    return true;
}

void BPlusTree::SplitLeaf(BPlusLeafNode* old_node, PageId old_page_id) {
    (void)old_page_id;
    PageId new_page_id;
    Page* new_page = buffer_pool_->NewPage(&new_page_id);
    auto* new_node = reinterpret_cast<BPlusLeafNode*>(new_page->data_);
    new_node->Init(new_page_id, old_node->header_.parent_page_id_, old_node->header_.max_size_);
    
    int split_idx = old_node->header_.size_ / 2;
    int move_count = old_node->header_.size_ - split_idx;
    
    for (int i = 0; i < move_count; i++) {
        new_node->keys_[i] = old_node->keys_[split_idx + i];
        new_node->values_[i] = old_node->values_[split_idx + i];
    }
    
    new_node->header_.size_ = move_count;
    old_node->header_.size_ = split_idx;
    
    new_node->next_page_id_ = old_node->next_page_id_;
    old_node->next_page_id_ = new_page_id;
    
    int32_t split_key = new_node->keys_[0];
    
    InsertIntoParent(reinterpret_cast<BPlusNodeHeader*>(old_node), split_key, 
                     reinterpret_cast<BPlusNodeHeader*>(new_node), new_page_id);
    
    buffer_pool_->UnpinPage(new_page_id, true);
}

void BPlusTree::InsertIntoParent(BPlusNodeHeader* old_node, int32_t split_key, BPlusNodeHeader* new_node, PageId new_page_id) {
    if (old_node->parent_page_id_ == INVALID_PAGE_ID) {
        // Create new root
        PageId new_root_id;
        Page* new_root_page = buffer_pool_->NewPage(&new_root_id);
        auto* new_root = reinterpret_cast<BPlusInternalNode*>(new_root_page->data_);
        new_root->Init(new_root_id, INVALID_PAGE_ID, 
            (PAGE_SIZE - sizeof(BPlusNodeHeader)) / (sizeof(int32_t) + sizeof(PageId)) - 1);
        
        new_root->keys_[0] = split_key;
        new_root->values_[0] = old_node->page_id_;
        new_root->values_[1] = new_page_id;
        new_root->header_.size_ = 1;
        
        old_node->parent_page_id_ = new_root_id;
        new_node->parent_page_id_ = new_root_id;
        
        UpdateRootId(new_root_id);
        
        buffer_pool_->UnpinPage(new_root_id, true);
        return;
    }
    
    PageId parent_id = old_node->parent_page_id_;
    Page* parent_page = buffer_pool_->FetchPage(parent_id);
    auto* parent_node = reinterpret_cast<BPlusInternalNode*>(parent_page->data_);
    
    int insert_idx = 0;
    while (insert_idx < parent_node->header_.size_ && split_key > parent_node->keys_[insert_idx]) {
        insert_idx++;
    }
    
    for (int i = parent_node->header_.size_; i > insert_idx; i--) {
        parent_node->keys_[i] = parent_node->keys_[i - 1];
        parent_node->values_[i + 1] = parent_node->values_[i];
    }
    
    parent_node->keys_[insert_idx] = split_key;
    parent_node->values_[insert_idx + 1] = new_page_id;
    parent_node->header_.size_++;
    
    if (parent_node->header_.size_ > parent_node->header_.max_size_) {
        SplitInternal(parent_node, parent_id);
    }
    
    buffer_pool_->UnpinPage(parent_id, true);
}

void BPlusTree::SplitInternal(BPlusInternalNode* old_node, PageId old_page_id) {
    (void)old_page_id;
    // Simplified minimal internal split logic
    PageId new_page_id;
    Page* new_page = buffer_pool_->NewPage(&new_page_id);
    auto* new_node = reinterpret_cast<BPlusInternalNode*>(new_page->data_);
    new_node->Init(new_page_id, old_node->header_.parent_page_id_, old_node->header_.max_size_);
    
    int split_idx = old_node->header_.size_ / 2;
    int32_t split_key = old_node->keys_[split_idx];
    
    int move_count = old_node->header_.size_ - split_idx - 1;
    
    for (int i = 0; i < move_count; i++) {
        new_node->keys_[i] = old_node->keys_[split_idx + 1 + i];
        new_node->values_[i] = old_node->values_[split_idx + 1 + i];
    }
    new_node->values_[move_count] = old_node->values_[old_node->header_.size_];
    
    new_node->header_.size_ = move_count;
    old_node->header_.size_ = split_idx;
    
    // Update parent pointers of children
    for (int i = 0; i <= new_node->header_.size_; i++) {
        PageId child_id = new_node->values_[i];
        Page* child_page = buffer_pool_->FetchPage(child_id);
        auto* child_node = reinterpret_cast<BPlusNodeHeader*>(child_page->data_);
        child_node->parent_page_id_ = new_page_id;
        buffer_pool_->UnpinPage(child_id, true);
    }
    
    InsertIntoParent(reinterpret_cast<BPlusNodeHeader*>(old_node), split_key, 
                     reinterpret_cast<BPlusNodeHeader*>(new_node), new_page_id);
    
    buffer_pool_->UnpinPage(new_page_id, true);
}

bool BPlusTree::Search(int32_t key, RecordId* result) {
    if (root_page_id_ == INVALID_PAGE_ID) return false;
    
    PageId leaf_page_id = FindLeafPage(key);
    Page* leaf_page = buffer_pool_->FetchPage(leaf_page_id);
    auto* leaf_node = reinterpret_cast<BPlusLeafNode*>(leaf_page->data_);
    
    bool found = false;
    for (int i = 0; i < leaf_node->header_.size_; i++) {
        if (leaf_node->keys_[i] == key) {
            *result = leaf_node->values_[i];
            found = true;
            break;
        }
    }
    
    buffer_pool_->UnpinPage(leaf_page_id, false);
    return found;
}

void BPlusTree::Remove(int32_t key) {
    if (root_page_id_ == INVALID_PAGE_ID) return;
    
    PageId leaf_page_id = FindLeafPage(key);
    Page* leaf_page = buffer_pool_->FetchPage(leaf_page_id);
    auto* leaf_node = reinterpret_cast<BPlusLeafNode*>(leaf_page->data_);
    
    for (int i = 0; i < leaf_node->header_.size_; i++) {
        if (leaf_node->keys_[i] == key) {
            // Logical delete: shift elements left
            for (int j = i; j < leaf_node->header_.size_ - 1; j++) {
                leaf_node->keys_[j] = leaf_node->keys_[j + 1];
                leaf_node->values_[j] = leaf_node->values_[j + 1];
            }
            leaf_node->header_.size_--;
            break;
        }
    }
    
    buffer_pool_->UnpinPage(leaf_page_id, true);
}

PageId BPlusTree::GetFirstLeafPageId() {
    if (root_page_id_ == INVALID_PAGE_ID) return INVALID_PAGE_ID;
    
    PageId current_page_id = root_page_id_;
    Page* current_page = buffer_pool_->FetchPage(current_page_id);
    auto* node = reinterpret_cast<BPlusNodeHeader*>(current_page->data_);
    
    while (node->type_ == IndexNodeType::INTERNAL) {
        auto* internal_node = reinterpret_cast<BPlusInternalNode*>(current_page->data_);
        PageId next_page_id = internal_node->values_[0];
        buffer_pool_->UnpinPage(current_page_id, false);
        
        current_page_id = next_page_id;
        current_page = buffer_pool_->FetchPage(current_page_id);
        node = reinterpret_cast<BPlusNodeHeader*>(current_page->data_);
    }
    
    buffer_pool_->UnpinPage(current_page_id, false);
    return current_page_id;
}

} // namespace minidb
