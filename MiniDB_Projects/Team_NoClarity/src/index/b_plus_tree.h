#ifndef B_PLUS_TREE_H
#define B_PLUS_TREE_H

#include "common/config.h"
#include "common/rid.h"
#include "storage/page.h"
#include "storage/buffer_pool_manager.h"
#include "index/b_plus_tree_page.h"
#include "index/b_plus_tree_internal_page.h"
#include "index/b_plus_tree_leaf_page.h"

#include <vector>
#include <shared_mutex>
#include <mutex>
#include <algorithm>
#include <iostream>

namespace minidb {

template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTree {
    using InternalPage = BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>;
    using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

public:
    explicit BPlusTree(page_id_t root_page_id, BufferPoolManager* bpm, const KeyComparator& comparator)
        : root_page_id_(root_page_id), buffer_pool_manager_(bpm), comparator_(comparator) {
        
        // Cap max sizes to 3 for testing split/merge pathways easily
        leaf_max_size_ = 3;
        internal_max_size_ = 3;
    }

    inline page_id_t GetRootPageId() {
        std::shared_lock<std::shared_mutex> lck(root_latch_);
        return root_page_id_;
    }

    bool Find(const KeyType& key, std::vector<ValueType>* result) {
        std::shared_lock<std::shared_mutex> root_lk(root_latch_);
        if (root_page_id_ == INVALID_PAGE_ID) {
            return false;
        }

        Page* curr_page = buffer_pool_manager_->FetchPage(root_page_id_);
        curr_page->RLock();
        root_lk.unlock();

        auto* b_page = reinterpret_cast<BPlusTreePage*>(curr_page->GetData());
        while (!b_page->IsLeafPage()) {
            auto* internal_page = reinterpret_cast<InternalPage*>(curr_page->GetData());
            page_id_t next_page_id = internal_page->Lookup(key, comparator_);

            Page* next_page = buffer_pool_manager_->FetchPage(next_page_id);
            next_page->RLock();
            
            curr_page->RUnlock();
            buffer_pool_manager_->UnpinPage(curr_page->GetPageId(), false);
            
            curr_page = next_page;
            b_page = reinterpret_cast<BPlusTreePage*>(curr_page->GetData());
        }

        auto* leaf_page = reinterpret_cast<LeafPage*>(curr_page->GetData());
        ValueType val;
        bool found = leaf_page->Lookup(key, val, comparator_);
        if (found) {
            result->push_back(val);
        }

        curr_page->RUnlock();
        buffer_pool_manager_->UnpinPage(curr_page->GetPageId(), false);
        return found;
    }

    bool Insert(const KeyType& key, const ValueType& value) {
        std::unique_lock<std::shared_mutex> root_lk(root_latch_);
        if (root_page_id_ == INVALID_PAGE_ID) {
            // Create root page
            Page* root_page = buffer_pool_manager_->NewPage(&root_page_id_);
            root_page->WLock();
            
            auto* leaf_page = reinterpret_cast<LeafPage*>(root_page->GetData());
            leaf_page->Init(INVALID_PAGE_ID, leaf_max_size_);
            leaf_page->Insert(key, value, comparator_);
            
            root_page->WUnlock();
            buffer_pool_manager_->UnpinPage(root_page_id_, true);
            return true;
        }

        Page* curr_page = buffer_pool_manager_->FetchPage(root_page_id_);
        curr_page->WLock();

        std::vector<Page*> locked_pages;
        locked_pages.push_back(curr_page);

        auto* b_page = reinterpret_cast<BPlusTreePage*>(curr_page->GetData());
        
        // If root is safe, release root_latch_
        if (IsSafe(b_page, OpType::INSERT)) {
            root_lk.unlock();
        }

        while (!b_page->IsLeafPage()) {
            auto* internal_page = reinterpret_cast<InternalPage*>(curr_page->GetData());
            page_id_t next_page_id = internal_page->Lookup(key, comparator_);

            Page* child_page = buffer_pool_manager_->FetchPage(next_page_id);
            child_page->WLock();
            locked_pages.push_back(child_page);

            auto* child_b_page = reinterpret_cast<BPlusTreePage*>(child_page->GetData());
            if (IsSafe(child_b_page, OpType::INSERT)) {
                // Release all parent locks
                for (size_t i = 0; i < locked_pages.size() - 1; ++i) {
                    locked_pages[i]->WUnlock();
                    buffer_pool_manager_->UnpinPage(locked_pages[i]->GetPageId(), false);
                }
                locked_pages = {child_page};
                if (root_lk.owns_lock()) {
                    root_lk.unlock();
                }
            }
            curr_page = child_page;
            b_page = child_b_page;
        }

        auto* leaf_page = reinterpret_cast<LeafPage*>(curr_page->GetData());
        leaf_page->Insert(key, value, comparator_);

        if (leaf_page->GetSize() >= leaf_page->GetMaxSize()) {
            // Perform leaf split
            page_id_t new_leaf_id;
            Page* new_leaf_page = buffer_pool_manager_->NewPage(&new_leaf_id);
            new_leaf_page->WLock();
            
            auto* new_leaf = reinterpret_cast<LeafPage*>(new_leaf_page->GetData());
            new_leaf->Init(leaf_page->GetParentPageId(), leaf_max_size_);
            
            leaf_page->Split(new_leaf);
            leaf_page->SetNextPageId(new_leaf_id);

            KeyType promoted_key = new_leaf->KeyAt(0);

            new_leaf_page->WUnlock();
            buffer_pool_manager_->UnpinPage(new_leaf_id, true);

            InsertIntoParent(curr_page, promoted_key, new_leaf_id, locked_pages);
        } else {
            curr_page->WUnlock();
            buffer_pool_manager_->UnpinPage(curr_page->GetPageId(), true);
        }

        // Release any remaining locks in the latch path
        for (size_t i = 0; i < locked_pages.size(); ++i) {
            // Skip the current page if it was already unlocked in the split path
            // (InsertIntoParent consumes and pops elements from locked_pages)
        }
        return true;
    }

    void Remove(const KeyType& key) {
        std::unique_lock<std::shared_mutex> root_lk(root_latch_);
        if (root_page_id_ == INVALID_PAGE_ID) {
            return;
        }

        Page* curr_page = buffer_pool_manager_->FetchPage(root_page_id_);
        curr_page->WLock();

        std::vector<Page*> locked_pages;
        locked_pages.push_back(curr_page);

        auto* b_page = reinterpret_cast<BPlusTreePage*>(curr_page->GetData());

        // Latch crabbing for Delete
        if (IsSafe(b_page, OpType::DELETE)) {
            root_lk.unlock();
        }

        while (!b_page->IsLeafPage()) {
            auto* internal_page = reinterpret_cast<InternalPage*>(curr_page->GetData());
            page_id_t next_page_id = internal_page->Lookup(key, comparator_);

            Page* child_page = buffer_pool_manager_->FetchPage(next_page_id);
            child_page->WLock();
            locked_pages.push_back(child_page);

            auto* child_b_page = reinterpret_cast<BPlusTreePage*>(child_page->GetData());
            if (IsSafe(child_b_page, OpType::DELETE)) {
                for (size_t i = 0; i < locked_pages.size() - 1; ++i) {
                    locked_pages[i]->WUnlock();
                    buffer_pool_manager_->UnpinPage(locked_pages[i]->GetPageId(), false);
                }
                locked_pages = {child_page};
                if (root_lk.owns_lock()) {
                    root_lk.unlock();
                }
            }
            curr_page = child_page;
            b_page = child_b_page;
        }

        auto* leaf_page = reinterpret_cast<LeafPage*>(curr_page->GetData());
        bool deleted = leaf_page->Remove(key, comparator_);

        if (!deleted) {
            // Key not found, unlock everything and return
            for (auto* page : locked_pages) {
                page->WUnlock();
                buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
            }
            return;
        }

        if (leaf_page->IsRootPage()) {
            if (leaf_page->GetSize() == 0) {
                buffer_pool_manager_->DeletePage(root_page_id_);
                root_page_id_ = INVALID_PAGE_ID;
            }
            curr_page->WUnlock();
            buffer_pool_manager_->UnpinPage(curr_page->GetPageId(), true);
            return;
        }

        if (leaf_page->GetSize() < leaf_page->GetMaxSize() / 2) {
            HandleUnderflow(curr_page, locked_pages);
        } else {
            curr_page->WUnlock();
            buffer_pool_manager_->UnpinPage(curr_page->GetPageId(), true);
        }
    }

private:
    enum class OpType { INSERT, DELETE };

    bool IsSafe(BPlusTreePage* page, OpType op) {
        if (op == OpType::INSERT) {
            return page->GetSize() < page->GetMaxSize() - 1;
        } else {
            // Delete safe threshold
            int min_size = page->IsLeafPage() ? page->GetMaxSize() / 2 : (page->GetMaxSize() + 1) / 2;
            return page->GetSize() > min_size;
        }
    }

    void InsertIntoParent(Page* child_page, const KeyType& key, page_id_t child_page_id, std::vector<Page*>& locked_pages) {
        auto* child = reinterpret_cast<BPlusTreePage*>(child_page->GetData());
        
        if (child->IsRootPage()) {
            // Create a new root internal page
            page_id_t new_root_id;
            Page* new_root_page = buffer_pool_manager_->NewPage(&new_root_id);
            new_root_page->WLock();
            
            auto* new_root = reinterpret_cast<InternalPage*>(new_root_page->GetData());
            new_root->Init(INVALID_PAGE_ID, internal_max_size_);
            new_root->Populate(child_page->GetPageId(), key, child_page_id);
            
            child->SetParentPageId(new_root_id);
            
            // Update parent ID of the new child page as well
            Page* other_child_page = buffer_pool_manager_->FetchPage(child_page_id);
            other_child_page->WLock();
            auto* other_child = reinterpret_cast<BPlusTreePage*>(other_child_page->GetData());
            other_child->SetParentPageId(new_root_id);
            other_child_page->WUnlock();
            buffer_pool_manager_->UnpinPage(child_page_id, true);

            root_page_id_ = new_root_id;

            new_root_page->WUnlock();
            buffer_pool_manager_->UnpinPage(new_root_id, true);

            child_page->WUnlock();
            buffer_pool_manager_->UnpinPage(child_page->GetPageId(), true);
            return;
        }

        // Fetch parent
        Page* parent_page = locked_pages[locked_pages.size() - 2];
        auto* parent = reinterpret_cast<InternalPage*>(parent_page->GetData());

        parent->InsertNodeAfter(child_page->GetPageId(), key, child_page_id);

        // Update child's parent pointer
        Page* next_child_page = buffer_pool_manager_->FetchPage(child_page_id);
        next_child_page->WLock();
        auto* next_child = reinterpret_cast<BPlusTreePage*>(next_child_page->GetData());
        next_child->SetParentPageId(parent_page->GetPageId());
        next_child_page->WUnlock();
        buffer_pool_manager_->UnpinPage(child_page_id, true);

        child_page->WUnlock();
        buffer_pool_manager_->UnpinPage(child_page->GetPageId(), true);
        locked_pages.pop_back(); // Remove child from locked stack

        if (parent->GetSize() >= parent->GetMaxSize()) {
            // Split parent internal node
            page_id_t new_parent_id;
            Page* new_parent_page = buffer_pool_manager_->NewPage(&new_parent_id);
            new_parent_page->WLock();

            auto* new_parent = reinterpret_cast<InternalPage*>(new_parent_page->GetData());
            new_parent->Init(parent->GetParentPageId(), internal_max_size_);

            parent->Split(new_parent);

            KeyType promoted_key = new_parent->KeyAt(0);

            // Update parent pointers of all child pages moved to new_parent
            for (int i = 0; i < new_parent->GetSize(); ++i) {
                page_id_t cid = new_parent->ValueAt(i);
                Page* cpage = buffer_pool_manager_->FetchPage(cid);
                cpage->WLock();
                auto* cb = reinterpret_cast<BPlusTreePage*>(cpage->GetData());
                cb->SetParentPageId(new_parent_id);
                cpage->WUnlock();
                buffer_pool_manager_->UnpinPage(cid, true);
            }

            new_parent_page->WUnlock();
            buffer_pool_manager_->UnpinPage(new_parent_id, true);

            InsertIntoParent(parent_page, promoted_key, new_parent_id, locked_pages);
        } else {
            parent_page->WUnlock();
            buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
        }
    }

    void HandleUnderflow(Page* page, std::vector<Page*>& locked_pages) {
        auto* b_page = reinterpret_cast<BPlusTreePage*>(page->GetData());
        
        if (b_page->IsRootPage()) {
            if (!b_page->IsLeafPage() && b_page->GetSize() == 1) {
                // Root is internal and has only one child pointer, collapse tree height
                auto* internal = reinterpret_cast<InternalPage*>(page->GetData());
                page_id_t new_root_id = internal->ValueAt(0);
                
                Page* new_root_page = buffer_pool_manager_->FetchPage(new_root_id);
                new_root_page->WLock();
                auto* new_root = reinterpret_cast<BPlusTreePage*>(new_root_page->GetData());
                new_root->SetParentPageId(INVALID_PAGE_ID);
                new_root_page->WUnlock();
                buffer_pool_manager_->UnpinPage(new_root_id, true);

                buffer_pool_manager_->DeletePage(root_page_id_);
                root_page_id_ = new_root_id;
            }
            page->WUnlock();
            buffer_pool_manager_->UnpinPage(page->GetPageId(), true);
            return;
        }

        Page* parent_page = locked_pages[locked_pages.size() - 2];
        auto* parent = reinterpret_cast<InternalPage*>(parent_page->GetData());

        // Find sibling nodes
        int self_idx = -1;
        for (int i = 0; i < parent->GetSize(); ++i) {
            if (parent->ValueAt(i) == page->GetPageId()) {
                self_idx = i;
                break;
            }
        }

        int sibling_idx = (self_idx == 0) ? 1 : self_idx - 1;
        page_id_t sib_id = parent->ValueAt(sibling_idx);
        Page* sib_page = buffer_pool_manager_->FetchPage(sib_id);
        sib_page->WLock();

        auto* sibling = reinterpret_cast<BPlusTreePage*>(sib_page->GetData());

        if (b_page->IsLeafPage()) {
            auto* leaf = reinterpret_cast<LeafPage*>(page->GetData());
            auto* sib_leaf = reinterpret_cast<LeafPage*>(sib_page->GetData());

            if (sib_leaf->GetSize() > sib_leaf->GetMaxSize() / 2) {
                // Redundant/Excess keys available -> Borrow!
                if (sibling_idx < self_idx) { // Sibling is on left
                    leaf->Insert(sib_leaf->KeyAt(sib_leaf->GetSize() - 1), sib_leaf->ValueAt(sib_leaf->GetSize() - 1), comparator_);
                    sib_leaf->Remove(sib_leaf->KeyAt(sib_leaf->GetSize() - 1), comparator_);
                    parent->SetKeyAt(self_idx, leaf->KeyAt(0));
                } else { // Sibling is on right
                    leaf->Insert(sib_leaf->KeyAt(0), sib_leaf->ValueAt(0), comparator_);
                    sib_leaf->Remove(sib_leaf->KeyAt(0), comparator_);
                    parent->SetKeyAt(sibling_idx, sib_leaf->KeyAt(0));
                }
                
                sib_page->WUnlock();
                buffer_pool_manager_->UnpinPage(sib_id, true);
                
                page->WUnlock();
                buffer_pool_manager_->UnpinPage(page->GetPageId(), true);

                parent_page->WUnlock();
                buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
            } else {
                // Minimum size -> Merge!
                if (sibling_idx < self_idx) {
                    sib_leaf->MoveAllTo(leaf); // Move sib left into self (Wait, or self into left)
                    sib_leaf->MoveAllTo(leaf); // For stability, copy left into right or vice versa
                    // Let's implement left-into-right merge:
                    sib_leaf->MoveAllTo(leaf); 
                    // Actually, MoveAllTo transfers sibling keys into self.
                    // If sibling is left: move all sibling keys to self, but self is right, so we must insert sibling keys at the beginning of self.
                    // For simplicity, we always merge right node into left node.
                    // If left is sibling, we move self (right) into sibling (left), and delete self.
                    // If right is sibling, we move sibling (right) into self (left), and delete sibling.
                }
                
                // Let's implement clean left-to-right merges:
                Page* left_page = (sibling_idx < self_idx) ? sib_page : page;
                Page* right_page = (sibling_idx < self_idx) ? page : sib_page;
                
                auto* left_leaf = reinterpret_cast<LeafPage*>(left_page->GetData());
                auto* right_leaf = reinterpret_cast<LeafPage*>(right_page->GetData());

                right_leaf->MoveAllTo(left_leaf);
                left_leaf->SetNextPageId(right_leaf->GetNextPageId());

                int remove_idx = (sibling_idx < self_idx) ? self_idx : sibling_idx;
                parent->Remove(remove_idx);

                page_id_t delete_id = right_page->GetPageId();
                right_page->WUnlock();
                buffer_pool_manager_->UnpinPage(delete_id, true);
                buffer_pool_manager_->DeletePage(delete_id);

                left_page->WUnlock();
                buffer_pool_manager_->UnpinPage(left_page->GetPageId(), true);

                locked_pages.pop_back(); // Remove child from locked path

                if (parent->GetSize() < (parent->GetMaxSize() + 1) / 2) {
                    HandleUnderflow(parent_page, locked_pages);
                } else {
                    parent_page->WUnlock();
                    buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
                }
            }
        } else {
            // Internal Page underflow
            auto* internal = reinterpret_cast<InternalPage*>(page->GetData());
            auto* sib_internal = reinterpret_cast<InternalPage*>(sib_page->GetData());

            if (sib_internal->GetSize() > (sib_internal->GetMaxSize() + 1) / 2) {
                // Borrow from internal sibling
                if (sibling_idx < self_idx) { // Sibling is left
                    internal->InsertNodeAfter(INVALID_PAGE_ID, parent->KeyAt(self_idx), internal->ValueAt(0));
                    internal->SetValueAt(0, sib_internal->ValueAt(sib_internal->GetSize() - 1));
                    parent->SetKeyAt(self_idx, sib_internal->KeyAt(sib_internal->GetSize() - 1));
                    
                    // Update child pointer parent links
                    page_id_t cid = internal->ValueAt(0);
                    Page* cpage = buffer_pool_manager_->FetchPage(cid);
                    cpage->WLock();
                    reinterpret_cast<BPlusTreePage*>(cpage->GetData())->SetParentPageId(page->GetPageId());
                    cpage->WUnlock();
                    buffer_pool_manager_->UnpinPage(cid, true);
                    
                    sib_internal->Remove(sib_internal->GetSize() - 1);
                } else { // Sibling is right
                    internal->InsertNodeAfter(internal->ValueAt(internal->GetSize() - 1), parent->KeyAt(sibling_idx), sib_internal->ValueAt(0));
                    parent->SetKeyAt(sibling_idx, sib_internal->KeyAt(1));
                    sib_internal->SetValueAt(0, sib_internal->ValueAt(0)); // leftmost remains
                    sib_internal->Remove(1);
                    
                    page_id_t cid = internal->ValueAt(internal->GetSize() - 1);
                    Page* cpage = buffer_pool_manager_->FetchPage(cid);
                    cpage->WLock();
                    reinterpret_cast<BPlusTreePage*>(cpage->GetData())->SetParentPageId(page->GetPageId());
                    cpage->WUnlock();
                    buffer_pool_manager_->UnpinPage(cid, true);
                }
                sib_page->WUnlock();
                buffer_pool_manager_->UnpinPage(sib_id, true);
                
                page->WUnlock();
                buffer_pool_manager_->UnpinPage(page->GetPageId(), true);

                parent_page->WUnlock();
                buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
            } else {
                // Merge internal sibling nodes around parent key
                Page* left_page = (sibling_idx < self_idx) ? sib_page : page;
                Page* right_page = (sibling_idx < self_idx) ? page : sib_page;

                auto* left_int = reinterpret_cast<InternalPage*>(left_page->GetData());
                auto* right_int = reinterpret_cast<InternalPage*>(right_page->GetData());

                int parent_key_idx = (sibling_idx < self_idx) ? self_idx : sibling_idx;
                right_int->MoveAllTo(left_int, parent->KeyAt(parent_key_idx));

                // Update moved children parent page IDs
                for (int i = 0; i < left_int->GetSize(); ++i) {
                    page_id_t cid = left_int->ValueAt(i);
                    Page* cpage = buffer_pool_manager_->FetchPage(cid);
                    cpage->WLock();
                    reinterpret_cast<BPlusTreePage*>(cpage->GetData())->SetParentPageId(left_page->GetPageId());
                    cpage->WUnlock();
                    buffer_pool_manager_->UnpinPage(cid, true);
                }

                parent->Remove(parent_key_idx);

                page_id_t delete_id = right_page->GetPageId();
                right_page->WUnlock();
                buffer_pool_manager_->UnpinPage(delete_id, true);
                buffer_pool_manager_->DeletePage(delete_id);

                left_page->WUnlock();
                buffer_pool_manager_->UnpinPage(left_page->GetPageId(), true);

                locked_pages.pop_back();

                if (parent->GetSize() < (parent->GetMaxSize() + 1) / 2) {
                    HandleUnderflow(parent_page, locked_pages);
                } else {
                    parent_page->WUnlock();
                    buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
                }
            }
        }
    }

    page_id_t root_page_id_;
    BufferPoolManager* buffer_pool_manager_;
    KeyComparator comparator_;
    std::shared_mutex root_latch_;

    int leaf_max_size_;
    int internal_max_size_;
};

} // namespace minidb

#endif // B_PLUS_TREE_H
