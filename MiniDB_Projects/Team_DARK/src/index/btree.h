#pragma once

#include "index/bnode.h"
#include "storage/buffer_pool_manager.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace minidb {

class BTree {
public:
    BTree(BufferPoolManager* buffer_pool_manager,
          page_id_t meta_page_id = BTREE_META_PAGE_ID,
          int degree = 50);

    bool Search(int64_t key, RecordId* out_rid) const;
    bool Insert(int64_t key, const RecordId& rid);
    bool Remove(int64_t key);
    std::vector<std::pair<int64_t, RecordId>> RangeSearch(int64_t low_key,
                                                          int64_t high_key) const;

    page_id_t RootPageId() const { return root_page_id_; }
    int Height() const;
    int Degree() const { return degree_; }
    page_id_t MetaPageId() const { return meta_page_id_; }

private:
    page_id_t AllocatePage();
    void InitializeNewTree();
    void LoadMeta();
    void SaveMeta();

    page_id_t FindLeafPage(int64_t key) const;
    int FindKeyIndex(const BNodePage& node, int64_t key) const;
    int FindChildIndex(const BNodePage& node, int64_t key) const;

    void SplitChild(page_id_t parent_page_id, int child_index);
    void InsertIntoLeaf(page_id_t leaf_page_id, int64_t key, const RecordId& rid);
    void InsertIntoParent(page_id_t parent_page_id, int insert_index, int64_t key,
                          page_id_t right_page_id);

    bool FindParentAndSlot(page_id_t child_page_id, page_id_t* out_parent,
                           int* out_slot) const;
    bool FindParentAndSlotHelper(page_id_t current, page_id_t child_page_id,
                                 page_id_t* out_parent, int* out_slot) const;

    int64_t GetPredecessorKey(page_id_t internal_page_id, int child_index) const;
    int64_t GetSuccessorKey(page_id_t internal_page_id, int child_index) const;

    void RemoveFromLeaf(page_id_t leaf_page_id, int key_index);
    void DeleteFromLeaf(page_id_t leaf_page_id, int64_t key);
    void EnsureLeafCapacity(page_id_t parent_page_id, int child_index);
    void BorrowFromLeft(page_id_t parent_page_id, int child_index);
    void BorrowFromRight(page_id_t parent_page_id, int child_index);
    void MergeLeaves(page_id_t parent_page_id, int left_index);
    void MergeChildren(page_id_t parent_page_id, int left_index);
    void ShrinkRootIfNeeded();

    BufferPoolManager* bpm_;
    page_id_t meta_page_id_;
    page_id_t root_page_id_;
    int degree_;
};

}  // namespace minidb
