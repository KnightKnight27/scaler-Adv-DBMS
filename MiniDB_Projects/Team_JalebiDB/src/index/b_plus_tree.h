#pragma once

#include "common/config.h"
#include "common/types.h"
#include "storage/buffer_pool_manager.h"
#include "index/b_plus_tree_node.h"
#include <mutex>

namespace minidb {

class BPlusTree {
public:
    BPlusTree(page_id_t root_page_id, BufferPoolManager *bpm);
    ~BPlusTree() = default;

    // Search for a key in the tree
    bool Search(int32_t key, RID &rid);

    // Insert a key-value pair
    bool Insert(int32_t key, RID rid);

    // Delete a key
    bool Delete(int32_t key);

    // Get the root page ID
    page_id_t GetRootPageId() const { return root_page_id_; }

    // Helper to print B+ Tree structure for viva/demo
    void PrintTree();

private:
    // Helper to find the leaf page for a key
    Page *FindLeafPage(int32_t key);

    // Helper to insert into a leaf node
    void InsertIntoLeaf(BPlusTreeNode &node, int32_t key, RID rid);

    // Helper to insert into an internal node
    void InsertIntoParent(page_id_t child_page_id, int32_t key, page_id_t new_child_page_id);

    // Helper to split a leaf node
    void SplitLeaf(BPlusTreeNode &leaf_node, int32_t new_key, RID new_rid);

    // Helper to split an internal node
    void SplitInternal(BPlusTreeNode &internal_node, int32_t new_key, page_id_t new_child_page_id);

    void PrintTreeHelper(page_id_t page_id, int depth);

    page_id_t root_page_id_{INVALID_PAGE_ID};
    BufferPoolManager *bpm_;
    std::mutex latch_;
};

} // namespace minidb
