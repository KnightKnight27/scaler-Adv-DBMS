#pragma once

#include "common/config.h"
#include "common/types.h"
#include "storage/page.h"

namespace minidb {

class BPlusTreeNode {
public:
    explicit BPlusTreeNode(Page *page) : page_(page) {}

    bool IsLeaf() const {
        return *reinterpret_cast<const bool *>(page_->GetData() + 12);
    }
    void SetIsLeaf(bool is_leaf) {
        *reinterpret_cast<bool *>(page_->GetData() + 12) = is_leaf;
    }

    uint16_t GetSize() const {
        return *reinterpret_cast<const uint16_t *>(page_->GetData() + 13);
    }
    void SetSize(uint16_t size) {
        *reinterpret_cast<uint16_t *>(page_->GetData() + 13) = size;
    }

    page_id_t GetParentPageId() const {
        return *reinterpret_cast<const page_id_t *>(page_->GetData() + 15);
    }
    void SetParentPageId(page_id_t parent_page_id) {
        *reinterpret_cast<page_id_t *>(page_->GetData() + 15) = parent_page_id;
    }

    page_id_t GetNextPageId() const {
        return *reinterpret_cast<const page_id_t *>(page_->GetData() + 19);
    }
    void SetNextPageId(page_id_t next_page_id) {
        *reinterpret_cast<page_id_t *>(page_->GetData() + 19) = next_page_id;
    }

    page_id_t GetPageId() const { return page_->GetPageId(); }

    // Key operations (for int32_t keys)
    int32_t GetKey(int index) const {
        const char *ptr = page_->GetData() + GetKeyOffset(index);
        return *reinterpret_cast<const int32_t *>(ptr);
    }
    void SetKey(int index, int32_t key) {
        char *ptr = page_->GetData() + GetKeyOffset(index);
        *reinterpret_cast<int32_t *>(ptr) = key;
    }

    // Leaf value operations (for RID values)
    RID GetVal(int index) const {
        const char *ptr = page_->GetData() + GetValOffset(index);
        return *reinterpret_cast<const RID *>(ptr);
    }
    void SetVal(int index, RID val) {
        char *ptr = page_->GetData() + GetValOffset(index);
        *reinterpret_cast<RID *>(ptr) = val;
    }

    // Internal value operations (for page_id_t children pointers)
    page_id_t GetChild(int index) const {
        const char *ptr = page_->GetData() + GetChildOffset(index);
        return *reinterpret_cast<const page_id_t *>(ptr);
    }
    void SetChild(int index, page_id_t child_page_id) {
        char *ptr = page_->GetData() + GetChildOffset(index);
        *reinterpret_cast<page_id_t *>(ptr) = child_page_id;
    }

    static constexpr int MaxKeysLeaf = 4;      // small for testing splits
    static constexpr int MaxKeysInternal = 4;  // small for testing splits

private:
    int GetKeyOffset(int index) const {
        if (IsLeaf()) {
            return 23 + index * 12; // 23 header + index * (4 key + 8 RID)
        } else {
            return 27 + index * 8;  // 23 header + 4 (child[0]) + index * (4 key + 4 child)
        }
    }

    int GetValOffset(int index) const {
        return GetKeyOffset(index) + 4;
    }

    int GetChildOffset(int index) const {
        if (index == 0) return 23; // child[0]
        return GetKeyOffset(index - 1) + 4; // child[index]
    }

    Page *page_;
};

} // namespace minidb
