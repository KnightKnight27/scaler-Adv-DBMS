#pragma once
#include "storage/page.h"
#include "storage/record_id.h"

namespace minidb {

enum class IndexNodeType { LEAF, INTERNAL };

// Base Header for B+ Tree Nodes
struct BPlusNodeHeader {
    IndexNodeType type_;
    int size_;
    int max_size_;
    PageId parent_page_id_;
    PageId page_id_;
};

// Internal Node: maps Key (int32_t) -> PageId
// The node contains N keys and N+1 pointers (page IDs).
// For simplicity, we just store array of keys and array of page IDs.
struct BPlusInternalNode {
    BPlusNodeHeader header_;
    // Layout: value_array[0], key_array[1], value_array[1], ...
    // To make it easy, we store parallel arrays
    int32_t keys_[ (PAGE_SIZE - sizeof(BPlusNodeHeader)) / (sizeof(int32_t) + sizeof(PageId)) ];
    PageId values_[ (PAGE_SIZE - sizeof(BPlusNodeHeader)) / (sizeof(int32_t) + sizeof(PageId)) ];
    
    void Init(PageId page_id, PageId parent_id, int max_size) {
        header_.type_ = IndexNodeType::INTERNAL;
        header_.size_ = 0;
        header_.max_size_ = max_size;
        header_.parent_page_id_ = parent_id;
        header_.page_id_ = page_id;
    }
};

// Leaf Node: maps Key (int32_t) -> RecordId
struct BPlusLeafNode {
    BPlusNodeHeader header_;
    PageId next_page_id_; // Pointer to next leaf node
    
    int32_t keys_[ (PAGE_SIZE - sizeof(BPlusNodeHeader) - sizeof(PageId)) / (sizeof(int32_t) + sizeof(RecordId)) ];
    RecordId values_[ (PAGE_SIZE - sizeof(BPlusNodeHeader) - sizeof(PageId)) / (sizeof(int32_t) + sizeof(RecordId)) ];

    void Init(PageId page_id, PageId parent_id, int max_size) {
        header_.type_ = IndexNodeType::LEAF;
        header_.size_ = 0;
        header_.max_size_ = max_size;
        header_.parent_page_id_ = parent_id;
        header_.page_id_ = page_id;
        next_page_id_ = INVALID_PAGE_ID;
    }
};

} // namespace minidb
