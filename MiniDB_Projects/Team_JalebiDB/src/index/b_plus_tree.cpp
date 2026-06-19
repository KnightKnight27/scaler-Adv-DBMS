#include "index/b_plus_tree.h"
#include <algorithm>
#include <iostream>
#include <vector>

namespace minidb {

BPlusTree::BPlusTree(page_id_t root_page_id, BufferPoolManager *bpm)
    : root_page_id_(root_page_id), bpm_(bpm) {}

Page *BPlusTree::FindLeafPage(int32_t key) {
    if (root_page_id_ == INVALID_PAGE_ID) {
        return nullptr;
    }
    
    Page *curr_page = bpm_->FetchPage(root_page_id_);
    BPlusTreeNode curr_node(curr_page);

    while (!curr_node.IsLeaf()) {
        int size = curr_node.GetSize();
        int child_index = 0;
        
        // Find which child to descend into
        while (child_index < size && key >= curr_node.GetKey(child_index)) {
            child_index++;
        }

        page_id_t next_page_id = curr_node.GetChild(child_index);
        bpm_->UnpinPage(curr_node.GetPageId(), false);
        curr_page = bpm_->FetchPage(next_page_id);
        curr_node = BPlusTreeNode(curr_page);
    }

    return curr_page;
}

bool BPlusTree::Search(int32_t key, RID &rid) {
    std::lock_guard<std::mutex> lock(latch_);
    Page *leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return false;
    }

    BPlusTreeNode leaf(leaf_page);
    int size = leaf.GetSize();
    bool found = false;
    
    for (int i = 0; i < size; ++i) {
        if (leaf.GetKey(i) == key) {
            rid = leaf.GetVal(i);
            found = true;
            break;
        }
    }

    bpm_->UnpinPage(leaf.GetPageId(), false);
    return found;
}

bool BPlusTree::Insert(int32_t key, RID rid) {
    std::lock_guard<std::mutex> lock(latch_);

    // 1. Empty tree case
    if (root_page_id_ == INVALID_PAGE_ID) {
        Page *root_page = bpm_->NewPage();
        if (root_page == nullptr) return false;
        
        root_page_id_ = root_page->GetPageId();
        BPlusTreeNode root(root_page);
        root.SetIsLeaf(true);
        root.SetSize(1);
        root.SetKey(0, key);
        root.SetVal(0, rid);
        root.SetParentPageId(INVALID_PAGE_ID);
        root.SetNextPageId(INVALID_PAGE_ID);
        
        bpm_->UnpinPage(root_page_id_, true);
        return true;
    }

    // 2. Find leaf page
    Page *leaf_page = FindLeafPage(key);
    BPlusTreeNode leaf(leaf_page);
    int size = leaf.GetSize();

    // Check duplicate key
    for (int i = 0; i < size; ++i) {
        if (leaf.GetKey(i) == key) {
            bpm_->UnpinPage(leaf.GetPageId(), false);
            return false; // primary key violation
        }
    }

    // 3. Leaf has space
    if (size < BPlusTreeNode::MaxKeysLeaf) {
        InsertIntoLeaf(leaf, key, rid);
        bpm_->UnpinPage(leaf.GetPageId(), true);
        return true;
    }

    // 4. Leaf is full, split it
    SplitLeaf(leaf, key, rid);
    return true;
}

void BPlusTree::InsertIntoLeaf(BPlusTreeNode &node, int32_t key, RID rid) {
    int size = node.GetSize();
    int idx = size;
    while (idx > 0 && node.GetKey(idx - 1) > key) {
        node.SetKey(idx, node.GetKey(idx - 1));
        node.SetVal(idx, node.GetVal(idx - 1));
        idx--;
    }
    node.SetKey(idx, key);
    node.SetVal(idx, rid);
    node.SetSize(size + 1);
}

void BPlusTree::SplitLeaf(BPlusTreeNode &leaf_node, int32_t new_key, RID new_rid) {
    // 1. Create sibling leaf page
    Page *sibling_page = bpm_->NewPage();
    BPlusTreeNode sibling_node(sibling_page);
    sibling_node.SetIsLeaf(true);
    sibling_node.SetParentPageId(leaf_node.GetParentPageId());
    sibling_node.SetNextPageId(leaf_node.GetNextPageId());
    leaf_node.SetNextPageId(sibling_node.GetPageId());

    // 2. Put all keys/vals into a sorted temporary array
    std::vector<std::pair<int32_t, RID>> temp;
    int size = leaf_node.GetSize();
    for (int i = 0; i < size; ++i) {
        temp.push_back({leaf_node.GetKey(i), leaf_node.GetVal(i)});
    }
    temp.push_back({new_key, new_rid});
    std::sort(temp.begin(), temp.end());

    // 3. Redistribute between leaf and sibling
    int split_idx = temp.size() / 2;
    leaf_node.SetSize(split_idx);
    for (int i = 0; i < split_idx; ++i) {
        leaf_node.SetKey(i, temp[i].first);
        leaf_node.SetVal(i, temp[i].second);
    }

    sibling_node.SetSize(temp.size() - split_idx);
    for (size_t i = split_idx; i < temp.size(); ++i) {
        int target_idx = i - split_idx;
        sibling_node.SetKey(target_idx, temp[i].first);
        sibling_node.SetVal(target_idx, temp[i].second);
    }

    page_id_t parent_page_id = leaf_node.GetParentPageId();
    int32_t first_sibling_key = sibling_node.GetKey(0);

    bpm_->UnpinPage(leaf_node.GetPageId(), true);
    bpm_->UnpinPage(sibling_node.GetPageId(), true);

    // 4. Insert parent key upwards
    InsertIntoParent(leaf_node.GetPageId(), first_sibling_key, sibling_node.GetPageId());
}

void BPlusTree::InsertIntoParent(page_id_t child_page_id, int32_t key, page_id_t new_child_page_id) {
    // If child was root, create new root
    if (child_page_id == root_page_id_) {
        Page *new_root_page = bpm_->NewPage();
        page_id_t new_root_id = new_root_page->GetPageId();
        BPlusTreeNode new_root(new_root_page);
        
        new_root.SetIsLeaf(false);
        new_root.SetSize(1);
        new_root.SetKey(0, key);
        new_root.SetChild(0, child_page_id);
        new_root.SetChild(1, new_child_page_id);
        new_root.SetParentPageId(INVALID_PAGE_ID);

        // Update child pages to point to new root
        Page *child = bpm_->FetchPage(child_page_id);
        BPlusTreeNode(child).SetParentPageId(new_root_id);
        bpm_->UnpinPage(child_page_id, true);

        Page *new_child = bpm_->FetchPage(new_child_page_id);
        BPlusTreeNode(new_child).SetParentPageId(new_root_id);
        bpm_->UnpinPage(new_child_page_id, true);

        root_page_id_ = new_root_id;
        bpm_->UnpinPage(new_root_id, true);
        return;
    }

    // Fetch parent
    Page *parent_page = bpm_->FetchPage(BPlusTreeNode(bpm_->FetchPage(child_page_id)).GetParentPageId());
    // (We also need to unpin the temp child page fetched inside the parameter list)
    {
        Page *temp_child = bpm_->FetchPage(child_page_id);
        bpm_->UnpinPage(child_page_id, false); // release immediately
        bpm_->UnpinPage(child_page_id, false); // release twice since fetched twice
    }

    BPlusTreeNode parent(parent_page);
    int size = parent.GetSize();

    if (size < BPlusTreeNode::MaxKeysInternal) {
        // Insert key and new_child_page_id in sorted order
        int idx = size;
        while (idx > 0 && parent.GetKey(idx - 1) > key) {
            parent.SetKey(idx, parent.GetKey(idx - 1));
            parent.SetChild(idx + 1, parent.GetChild(idx));
            idx--;
        }
        parent.SetKey(idx, key);
        parent.SetChild(idx + 1, new_child_page_id);
        parent.SetSize(size + 1);

        bpm_->UnpinPage(parent.GetPageId(), true);
    } else {
        // Parent is full, split parent
        SplitInternal(parent, key, new_child_page_id);
    }
}

void BPlusTree::SplitInternal(BPlusTreeNode &internal_node, int32_t new_key, page_id_t new_child_page_id) {
    // 1. Create sibling internal page
    Page *sibling_page = bpm_->NewPage();
    BPlusTreeNode sibling_node(sibling_page);
    sibling_node.SetIsLeaf(false);
    sibling_node.SetParentPageId(internal_node.GetParentPageId());

    // 2. Put all keys/children into temporary arrays
    int size = internal_node.GetSize();
    std::vector<int32_t> temp_keys;
    std::vector<page_id_t> temp_children;

    temp_children.push_back(internal_node.GetChild(0));
    for (int i = 0; i < size; ++i) {
        temp_keys.push_back(internal_node.GetKey(i));
        temp_children.push_back(internal_node.GetChild(i + 1));
    }

    // Insert new key and child
    auto it = std::lower_bound(temp_keys.begin(), temp_keys.end(), new_key);
    int idx = std::distance(temp_keys.begin(), it);
    temp_keys.insert(it, new_key);
    temp_children.insert(temp_children.begin() + idx + 1, new_child_page_id);

    // 3. Redistribute keys and children
    int split_idx = temp_keys.size() / 2;
    int32_t push_up_key = temp_keys[split_idx];

    // Left child updates
    internal_node.SetSize(split_idx);
    for (int i = 0; i < split_idx; ++i) {
        internal_node.SetKey(i, temp_keys[i]);
    }
    for (int i = 0; i <= split_idx; ++i) {
        internal_node.SetChild(i, temp_children[i]);
    }

    // Right child updates
    sibling_node.SetSize(temp_keys.size() - split_idx - 1);
    for (size_t i = split_idx + 1; i < temp_keys.size(); ++i) {
        int target_idx = i - (split_idx + 1);
        sibling_node.SetKey(target_idx, temp_keys[i]);
    }
    for (size_t i = split_idx + 1; i < temp_children.size(); ++i) {
        int target_idx = i - (split_idx + 1);
        sibling_node.SetChild(target_idx, temp_children[i]);
        
        // Update parent of moved child to point to sibling node
        Page *child = bpm_->FetchPage(temp_children[i]);
        BPlusTreeNode(child).SetParentPageId(sibling_node.GetPageId());
        bpm_->UnpinPage(temp_children[i], true);
    }

    bpm_->UnpinPage(internal_node.GetPageId(), true);
    bpm_->UnpinPage(sibling_node.GetPageId(), true);

    // 4. Recurse upwards
    InsertIntoParent(internal_node.GetPageId(), push_up_key, sibling_node.GetPageId());
}

bool BPlusTree::Delete(int32_t key) {
    std::lock_guard<std::mutex> lock(latch_);
    Page *leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return false;
    }

    BPlusTreeNode leaf(leaf_page);
    int size = leaf.GetSize();
    int delete_idx = -1;
    for (int i = 0; i < size; ++i) {
        if (leaf.GetKey(i) == key) {
            delete_idx = i;
            break;
        }
    }

    if (delete_idx == -1) {
        bpm_->UnpinPage(leaf.GetPageId(), false);
        return false;
    }

    // Shift keys/values left
    for (int i = delete_idx; i < size - 1; ++i) {
        leaf.SetKey(i, leaf.GetKey(i + 1));
        leaf.SetVal(i, leaf.GetVal(i + 1));
    }
    leaf.SetSize(size - 1);
    
    bpm_->UnpinPage(leaf.GetPageId(), true);
    return true;
}

void BPlusTree::PrintTree() {
    std::lock_guard<std::mutex> lock(latch_);
    std::cout << "=== B+ TREE STRUCTURE (Root Page: " << root_page_id_ << ") ===" << std::endl;
    if (root_page_id_ == INVALID_PAGE_ID) {
        std::cout << "Empty Tree" << std::endl;
        return;
    }
    PrintTreeHelper(root_page_id_, 0);
}

void BPlusTree::PrintTreeHelper(page_id_t page_id, int depth) {
    Page *page = bpm_->FetchPage(page_id);
    BPlusTreeNode node(page);

    std::string indent(depth * 4, ' ');
    if (node.IsLeaf()) {
        std::cout << indent << "Leaf [Page " << page_id << "] Keys: ";
        for (int i = 0; i < node.GetSize(); ++i) {
            std::cout << node.GetKey(i) << " ";
        }
        std::cout << std::endl;
    } else {
        std::cout << indent << "Internal [Page " << page_id << "] Keys: ";
        for (int i = 0; i < node.GetSize(); ++i) {
            std::cout << node.GetKey(i) << " ";
        }
        std::cout << " | Children: ";
        for (int i = 0; i <= node.GetSize(); ++i) {
            std::cout << node.GetChild(i) << " ";
        }
        std::cout << std::endl;

        std::vector<page_id_t> children;
        for (int i = 0; i <= node.GetSize(); ++i) {
            children.push_back(node.GetChild(i));
        }
        bpm_->UnpinPage(page_id, false);

        for (page_id_t child_id : children) {
            PrintTreeHelper(child_id, depth + 1);
        }
        return;
    }
    bpm_->UnpinPage(page_id, false);
}

} // namespace minidb
