#ifndef B_PLUS_TREE_INTERNAL_PAGE_H
#define B_PLUS_TREE_INTERNAL_PAGE_H

#include "index/b_plus_tree_page.h"
#include <utility>

namespace minidb {

/**
 * Internal routing nodes inside B+ Tree index structures, linking boundary keys to subpages.
 */
template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTreeInternalPage : public BPlusTreePage {
public:
    void Init(page_id_t parent_id = INVALID_PAGE_ID, int max_size = 3) {
        SetPageType(BPlusTreePageType::INTERNAL);
        SetSize(0);
        SetMaxSize(max_size);
        SetParentPageId(parent_id);
    }

    inline KeyType KeyAt(int index) const { return array_[index].first; }
    inline void SetKeyAt(int index, const KeyType& key) { array_[index].first = key; }

    inline ValueType ValueAt(int index) const { return array_[index].second; }
    inline void SetValueAt(int index, const ValueType& value) { array_[index].second = value; }

    // Performs binary boundary key lookups to determine branch direction paths
    ValueType Lookup(const KeyType& key, const KeyComparator& comparator) const {
        if (GetSize() <= 1) return array_[0].second;
        
        // Binary search on array_[1] to array_[size_-1]
        int low = 1, high = GetSize() - 1;
        int ans = 0; // fallback to value_0 if smaller than key1
        
        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (comparator(array_[mid].first, key) || array_[mid].first == key) {
                ans = mid;
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        return array_[ans].second;
    }

    void Populate(const ValueType& val0, const KeyType& key1, const ValueType& val1) {
        SetValueAt(0, val0);
        SetKeyAt(1, key1);
        SetValueAt(1, val1);
        SetSize(2);
    }

    void InsertNodeAfter(const ValueType& old_value, const KeyType& new_key, const ValueType& new_value) {
        int index = -1;
        for (int i = 0; i < GetSize(); ++i) {
            if (array_[i].second == old_value) {
                index = i;
                break;
            }
        }
        if (index == -1) return;

        for (int i = GetSize(); i > index + 1; --i) {
            array_[i] = array_[i - 1];
        }
        array_[index + 1] = {new_key, new_value};
        IncreaseSize(1);
    }

    void Split(BPlusTreeInternalPage* recipient) {
        int move_count = GetSize() / 2;
        int start_idx = GetSize() - move_count;

        recipient->SetSize(0);
        for (int i = start_idx; i < GetSize(); ++i) {
            recipient->array_[i - start_idx] = array_[i];
            recipient->IncreaseSize(1);
        }
        IncreaseSize(-move_count);
    }

    void Remove(int index) {
        for (int i = index; i < GetSize() - 1; ++i) {
            array_[i] = array_[i + 1];
        }
        IncreaseSize(-1);
    }

    void MoveAllTo(BPlusTreeInternalPage* recipient, const KeyType& middle_key) {
        int start_idx = recipient->GetSize();
        recipient->SetKeyAt(start_idx, middle_key);
        recipient->SetValueAt(start_idx, array_[0].second);
        recipient->IncreaseSize(1);

        for (int i = 1; i < GetSize(); ++i) {
            recipient->array_[recipient->GetSize()] = array_[i];
            recipient->IncreaseSize(1);
        }
        SetSize(0);
    }

private:
    std::pair<KeyType, ValueType> array_[1];
};

} // namespace minidb

#endif // B_PLUS_TREE_INTERNAL_PAGE_H
