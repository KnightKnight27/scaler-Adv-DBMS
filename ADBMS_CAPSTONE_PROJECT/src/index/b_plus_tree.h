#pragma once
#include "index/b_plus_node.h"
#include "storage/buffer_pool.h"

namespace minidb {

class BPlusTree {
public:
    BPlusTree(BufferPool* buffer_pool, PageId root_page_id = INVALID_PAGE_ID);

    // Returns true if insertion succeeds
    bool Insert(int32_t key, const RecordId& value);

    // Find value by key, returns true if found
    bool Search(int32_t key, RecordId* result);

    // Logical delete - we don't merge, just remove the record ID
    void Remove(int32_t key);

    // For range scans / full scans
    PageId GetFirstLeafPageId();

private:
    void StartNewTree(int32_t key, const RecordId& value);
    bool InsertIntoLeaf(int32_t key, const RecordId& value);
    void SplitLeaf(BPlusLeafNode* leaf_node, PageId leaf_page_id);
    void InsertIntoParent(BPlusNodeHeader* old_node, int32_t split_key, BPlusNodeHeader* new_node, PageId new_page_id);
    void SplitInternal(BPlusInternalNode* internal_node, PageId internal_page_id);
    
    PageId FindLeafPage(int32_t key, bool exclusive_lock = false);

    void UpdateRootId(PageId new_root);

    BufferPool* buffer_pool_;
    PageId root_page_id_;
    PageId meta_page_id_;
    std::mutex root_latch_;
};

} // namespace minidb
