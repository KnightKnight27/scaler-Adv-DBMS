#ifndef B_PLUS_TREE_LEAF_PAGE_H
#define B_PLUS_TREE_LEAF_PAGE_H

#include "index/b_plus_tree_page.h"
#include <utility>

namespace minidb {

/**
 * Structural leaf node pages inside B+ Tree index mapping keys to data tuple record identifiers.
 */
template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTreeLeafPage : public BPlusTreePage {
public:
    void Init(page_id_t parent_id = INVALID_PAGE_ID, int max_size = 3) {
        SetPageType(BPlusTreePageType::LEAF);
        SetSize(0);
        SetMaxSize(max_size);
        SetParentPageId(parent_id);
        SetNextPageId(INVALID_PAGE_ID);
    }

    inline page_id_t GetNextPageId() const { return next_page_id_; }
    inline void SetNextPageId(page_id_t next_page_id) { next_page_id_ = next_page_id; }

    inline KeyType KeyAt(int index) const { return array_[index].first; }
    inline void SetKeyAt(int index, const KeyType& key) { array_[index].first = key; }

    inline ValueType ValueAt(int index) const { return array_[index].second; }
    inline void SetValueAt(int index, const ValueType& value) { array_[index].second = value; }

    // Evaluates key presence inside leaf node structures
    bool Lookup(const KeyType& key, ValueType& result, const KeyComparator& comparator) const {
        int idx = KeyIndex(key, comparator);
        if (idx < GetSize() && !comparator(key, array_[idx].first) && !comparator(array_[idx].first, key)) {
            result = array_[idx].second;
            return true;
        }
        return false;
    }

    // Resolves key slot offset locations inside leaf key array
    int KeyIndex(const KeyType& key, const KeyComparator& comparator) const {
        int low = 0, high = GetSize() - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (comparator(array_[mid].first, key)) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        return low;
    }

    int Insert(const KeyType& key, const ValueType& value, const KeyComparator& comparator) {
        int idx = KeyIndex(key, comparator);
        // Overwrite if key already exists, or insert new
        if (idx < GetSize() && !comparator(key, array_[idx].first) && !comparator(array_[idx].first, key)) {
            array_[idx].second = value;
            return GetSize();
        }

        // Shift right
        for (int i = GetSize(); i > idx; --i) {
            array_[i] = array_[i - 1];
        }
        array_[idx] = {key, value};
        IncreaseSize(1);
        return GetSize();
    }

    void Split(BPlusTreeLeafPage* recipient) {
        int move_count = GetSize() / 2;
        int start_idx = GetSize() - move_count;

        recipient->SetSize(0);
        for (int i = start_idx; i < GetSize(); ++i) {
            recipient->array_[i - start_idx] = array_[i];
            recipient->IncreaseSize(1);
        }
        IncreaseSize(-move_count);
        
        recipient->SetNextPageId(next_page_id_);
        // next_page_id_ will be set to recipient's page id by the BPlusTree caller
    }

    bool Remove(const KeyType& key, const KeyComparator& comparator) {
        int idx = KeyIndex(key, comparator);
        if (idx >= GetSize() || comparator(key, array_[idx].first) || comparator(array_[idx].first, key)) {
            return false;
        }

        // Shift left
        for (int i = idx; i < GetSize() - 1; ++i) {
            array_[i] = array_[i + 1];
        }
        IncreaseSize(-1);
        return true;
    }

    void MoveAllTo(BPlusTreeLeafPage* recipient) {
        for (int i = 0; i < GetSize(); ++i) {
            recipient->array_[recipient->GetSize()] = array_[i];
            recipient->IncreaseSize(1);
        }
        recipient->SetNextPageId(next_page_id_);
        SetSize(0);
    }

private:
    page_id_t next_page_id_;
    std::pair<KeyType, ValueType> array_[1];
};

} // namespace minidb

#endif // B_PLUS_TREE_LEAF_PAGE_H
