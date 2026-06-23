#pragma once
#include "storage/buffer_pool.h"
#include <vector>

namespace minidb {

enum class IndexNodeType { LEAF = 0, INTERNAL = 1 };

// Common Node Header (16 bytes)
// 0: node_type (4 bytes)
// 4: size (4 bytes)
// 8: max_size (4 bytes)
// 12: parent_page_id (4 bytes)

class BPlusTree {
public:
    BPlusTree(BufferPool *buffer_pool, page_id_t root_page_id = INVALID_PAGE_ID);
    
    bool Insert(int32_t key, const RecordId &rid);
    bool Delete(int32_t key);
    bool Search(int32_t key, RecordId *result);
    
    class Iterator {
    public:
        Iterator(BufferPool *pool, page_id_t leaf_page_id, int index);
        ~Iterator();
        
        bool IsEnd();
        void Advance();
        int32_t GetKey();
        RecordId GetRid();
        
    private:
        BufferPool *pool_;
        page_id_t current_page_id_;
        Page *current_page_;
        int current_index_;
        
        void FetchPage();
        void UnpinPage();
    };
    
    Iterator Begin();
    Iterator Begin(int32_t key);
    
    page_id_t GetRootPageId() const { return root_page_id_; }

private:
    BufferPool *buffer_pool_;
    page_id_t root_page_id_;

    page_id_t FindLeafPage(int32_t key);
    void InsertIntoLeaf(int32_t key, const RecordId &rid, page_id_t leaf_page_id);
    page_id_t SplitLeaf(page_id_t leaf_page_id, int32_t *split_key);
    void InsertIntoParent(page_id_t left_page_id, int32_t key, page_id_t right_page_id);
    page_id_t SplitInternal(page_id_t internal_page_id, int32_t *split_key);
    
    void HandleLeafUnderflow(page_id_t leaf_page_id);
    
    void StartNewTree(int32_t key, const RecordId &rid);
    
    // Node access helpers
    bool IsLeaf(Page *page) const;
    int GetSize(Page *page) const;
    void SetSize(Page *page, int size);
    int GetMaxSize(Page *page) const;
    void SetMaxSize(Page *page, int max_size);
    page_id_t GetParent(Page *page) const;
    void SetParent(Page *page, page_id_t parent);
    void SetNodeType(Page *page, IndexNodeType type);
    
    // Internal node helpers
    int32_t InternalKeyAt(Page *page, int index) const;
    page_id_t InternalValueAt(Page *page, int index) const;
    void SetInternalKeyAt(Page *page, int index, int32_t key);
    void SetInternalValueAt(Page *page, int index, page_id_t val);
    
    // Leaf node helpers
    int32_t LeafKeyAt(Page *page, int index) const;
    RecordId LeafValueAt(Page *page, int index) const;
    void SetLeafKeyAt(Page *page, int index, int32_t key);
    void SetLeafValueAt(Page *page, int index, const RecordId &val);
    page_id_t GetNextLeaf(Page *page) const;
    void SetNextLeaf(Page *page, page_id_t next_page_id);
};

} // namespace minidb
