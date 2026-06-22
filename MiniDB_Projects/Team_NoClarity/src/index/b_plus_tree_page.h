#ifndef B_PLUS_TREE_PAGE_H
#define B_PLUS_TREE_PAGE_H

#include "common/config.h"

namespace minidb {

enum class BPlusTreePageType { LEAF = 0, INTERNAL = 1 };

class BPlusTreePage {
public:
    inline bool IsLeafPage() const { return page_type_ == BPlusTreePageType::LEAF; }
    inline bool IsRootPage() const { return parent_page_id_ == INVALID_PAGE_ID; }
    
    inline void SetPageType(BPlusTreePageType type) { page_type_ = type; }
    
    inline int GetSize() const { return size_; }
    inline void SetSize(int size) { size_ = size; }
    inline void IncreaseSize(int amount) { size_ += amount; }

    inline int GetMaxSize() const { return max_size_; }
    inline void SetMaxSize(int max_size) { max_size_ = max_size; }

    inline page_id_t GetParentPageId() const { return parent_page_id_; }
    inline void SetParentPageId(page_id_t parent_page_id) { parent_page_id_ = parent_page_id; }

protected:
    BPlusTreePageType page_type_;
    int size_;
    int max_size_;
    page_id_t parent_page_id_;
};

} // namespace minidb

#endif // B_PLUS_TREE_PAGE_H
