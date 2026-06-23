#include "index/btree.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace minidb {

namespace {

void InitLeafPage(char* page_data, page_id_t next_leaf) {
    std::memset(page_data, 0, PAGE_SIZE);
    BNodePage page(page_data);
    BNodeHeader header{};
    header.is_leaf = 1;
    header.key_count = 0;
    header.next_leaf_page_id = next_leaf;
    page.SetHeader(header);
}

void InitInternalPage(char* page_data) {
    std::memset(page_data, 0, PAGE_SIZE);
    BNodePage page(page_data);
    BNodeHeader header{};
    header.is_leaf = 0;
    header.key_count = 0;
    header.next_leaf_page_id = INVALID_PAGE_ID;
    page.SetHeader(header);
}

void ShiftKeysRight(BNodePage& page, int degree, std::size_t start, std::size_t count) {
    for (std::size_t i = count; i > start; --i) {
        page.SetKey(degree, i, page.GetKey(degree, i - 1));
    }
}

void ShiftRecordsRight(BNodePage& page, int degree, std::size_t start, std::size_t count) {
    for (std::size_t i = count; i > start; --i) {
        page.SetRecord(degree, i, page.GetRecord(degree, i - 1));
    }
}

void ShiftChildrenRight(BNodePage& page, int degree, std::size_t start, std::size_t count) {
    for (std::size_t i = count + 1; i > start + 1; --i) {
        page.SetChild(degree, i, page.GetChild(degree, i - 1));
    }
}

void ShiftKeysLeft(BNodePage& page, int degree, std::size_t gap_index, std::size_t last_index) {
    for (std::size_t i = gap_index; i < last_index; ++i) {
        page.SetKey(degree, i, page.GetKey(degree, i + 1));
    }
}

void ShiftRecordsLeft(BNodePage& page, int degree, std::size_t gap_index, std::size_t last_index) {
    for (std::size_t i = gap_index; i < last_index; ++i) {
        page.SetRecord(degree, i, page.GetRecord(degree, i + 1));
    }
}

void ShiftChildrenLeft(BNodePage& page, int degree, std::size_t start, std::size_t count) {
    for (std::size_t i = start; i <= count; ++i) {
        page.SetChild(degree, i, page.GetChild(degree, i + 1));
    }
}

bool NodeIsFull(const BNodePage& node, int degree) {
    return node.GetHeader().key_count >= static_cast<uint16_t>(2 * degree - 1);
}

}  // namespace

BTree::BTree(BufferPoolManager* buffer_pool_manager, page_id_t meta_page_id, int degree)
    : bpm_(buffer_pool_manager),
      meta_page_id_(meta_page_id),
      root_page_id_(INVALID_PAGE_ID),
      degree_(degree) {
    if (bpm_ == nullptr) {
        throw std::invalid_argument("buffer_pool_manager must not be null");
    }
    if (degree_ < 2) {
        throw std::invalid_argument("degree must be at least 2");
    }
    if (degree_ > BNodePage::MaxSupportedDegree()) {
        throw std::invalid_argument("degree exceeds page capacity");
    }

    char* meta_bytes = bpm_->FetchPage(meta_page_id_);
    MetaPageData meta = MetaPage(meta_bytes).Read();
    bpm_->UnpinPage(meta_page_id_);

    if (meta.magic == BTREE_META_MAGIC && meta.degree_t == static_cast<uint32_t>(degree_)) {
        root_page_id_ = meta.root_page_id;
        return;
    }

    InitializeNewTree();
}

void BTree::InitializeNewTree() {
    root_page_id_ = AllocatePage();
    char* root_bytes = bpm_->FetchPage(root_page_id_);
    InitLeafPage(root_bytes, INVALID_PAGE_ID);
    bpm_->MarkDirty(root_page_id_);
    bpm_->UnpinPage(root_page_id_);
    SaveMeta();
}

void BTree::LoadMeta() {
    char* meta_bytes = bpm_->FetchPage(meta_page_id_);
    MetaPageData meta = MetaPage(meta_bytes).Read();
    bpm_->UnpinPage(meta_page_id_);

    if (meta.magic != BTREE_META_MAGIC) {
        throw std::runtime_error("invalid B+ tree meta page");
    }

    root_page_id_ = meta.root_page_id;
    degree_ = static_cast<int>(meta.degree_t);
}

void BTree::SaveMeta() {
    char* meta_bytes = bpm_->FetchPage(meta_page_id_);
    MetaPageData meta = MetaPage(meta_bytes).Read();
    meta.root_page_id = root_page_id_;
    if (meta.magic != BTREE_META_MAGIC) {
        meta.next_page_id = meta_page_id_ + 1;
    }
    meta.degree_t = static_cast<uint32_t>(degree_);
    meta.magic = BTREE_META_MAGIC;
    MetaPage(meta_bytes).Write(meta);
    bpm_->MarkDirty(meta_page_id_);
    bpm_->UnpinPage(meta_page_id_);
}

page_id_t BTree::AllocatePage() {
    char* meta_bytes = bpm_->FetchPage(meta_page_id_);
    MetaPage meta(meta_bytes);
    MetaPageData data = meta.Read();

    if (data.magic != BTREE_META_MAGIC) {
        data.magic = BTREE_META_MAGIC;
        data.root_page_id = INVALID_PAGE_ID;
        data.next_page_id = meta_page_id_ + 1;
        data.degree_t = static_cast<uint32_t>(degree_);
    }

    const page_id_t new_page_id = data.next_page_id;
    data.next_page_id = new_page_id + 1;
    meta.Write(data);
    bpm_->MarkDirty(meta_page_id_);
    bpm_->UnpinPage(meta_page_id_);

    return new_page_id;
}

int BTree::FindKeyIndex(const BNodePage& node, int64_t key) const {
    BNodeHeader header = node.GetHeader();
    int index = 0;
    while (index < header.key_count && key > node.GetKey(degree_, static_cast<std::size_t>(index))) {
        ++index;
    }
    return index;
}

int BTree::FindChildIndex(const BNodePage& node, int64_t key) const {
    BNodeHeader header = node.GetHeader();
    int index = header.key_count - 1;
    while (index >= 0 && key < node.GetKey(degree_, static_cast<std::size_t>(index))) {
        --index;
    }
    return index + 1;
}

page_id_t BTree::FindLeafPage(int64_t key) const {
    page_id_t page_id = root_page_id_;
    while (true) {
        char* page_bytes = bpm_->FetchPage(page_id);
        BNodePage node(page_bytes);
        BNodeHeader header = node.GetHeader();
        if (header.is_leaf != 0) {
            bpm_->UnpinPage(page_id);
            return page_id;
        }

        const int child_index = FindChildIndex(node, key);
        const page_id_t child_page_id = node.GetChild(degree_, static_cast<std::size_t>(child_index));
        bpm_->UnpinPage(page_id);
        page_id = child_page_id;
    }
}

bool BTree::Search(int64_t key, RecordId* out_rid) const {
    const page_id_t leaf_page_id = FindLeafPage(key);
    char* page_bytes = bpm_->FetchPage(leaf_page_id);
    BNodePage node(page_bytes);
    BNodeHeader header = node.GetHeader();

    const int index = FindKeyIndex(node, key);
    bool found = false;
    if (index < header.key_count && node.GetKey(degree_, static_cast<std::size_t>(index)) == key) {
        if (out_rid != nullptr) {
            *out_rid = node.GetRecord(degree_, static_cast<std::size_t>(index));
        }
        found = true;
    }

    bpm_->UnpinPage(leaf_page_id);
    return found;
}

bool BTree::FindParentAndSlotHelper(page_id_t current, page_id_t child_page_id,
                                    page_id_t* out_parent, int* out_slot) const {
    char* page_bytes = bpm_->FetchPage(current);
    BNodePage node(page_bytes);
    BNodeHeader header = node.GetHeader();
    if (header.is_leaf != 0) {
        bpm_->UnpinPage(current);
        return false;
    }

    for (int i = 0; i <= header.key_count; ++i) {
        if (node.GetChild(degree_, static_cast<std::size_t>(i)) == child_page_id) {
            *out_parent = current;
            *out_slot = i;
            bpm_->UnpinPage(current);
            return true;
        }
    }

    for (int i = 0; i <= header.key_count; ++i) {
        const page_id_t child = node.GetChild(degree_, static_cast<std::size_t>(i));
        if (FindParentAndSlotHelper(child, child_page_id, out_parent, out_slot)) {
            bpm_->UnpinPage(current);
            return true;
        }
    }

    bpm_->UnpinPage(current);
    return false;
}

bool BTree::FindParentAndSlot(page_id_t child_page_id, page_id_t* out_parent,
                              int* out_slot) const {
    if (child_page_id == root_page_id_) {
        return false;
    }
    return FindParentAndSlotHelper(root_page_id_, child_page_id, out_parent, out_slot);
}

void BTree::InsertIntoParent(page_id_t parent_page_id, int insert_index, int64_t key,
                             page_id_t right_page_id) {
    int64_t cur_key = key;
    page_id_t cur_right = right_page_id;
    int cur_index = insert_index;
    page_id_t cur_parent = parent_page_id;

    while (cur_parent != INVALID_PAGE_ID) {
        char* parent_bytes = bpm_->FetchPage(cur_parent);
        BNodePage parent(parent_bytes);
        BNodeHeader parent_header = parent.GetHeader();

        if (!NodeIsFull(parent, degree_)) {
            ShiftKeysRight(parent, degree_, static_cast<std::size_t>(cur_index),
                           static_cast<std::size_t>(parent_header.key_count));
            ShiftChildrenRight(parent, degree_, static_cast<std::size_t>(cur_index),
                               static_cast<std::size_t>(parent_header.key_count));
            parent.SetKey(degree_, static_cast<std::size_t>(cur_index), cur_key);
            parent.SetChild(degree_, static_cast<std::size_t>(cur_index + 1), cur_right);
            parent_header.key_count += 1;
            parent.SetHeader(parent_header);
            bpm_->MarkDirty(cur_parent);
            bpm_->UnpinPage(cur_parent);
            return;
        }

        bpm_->UnpinPage(cur_parent);

        const page_id_t right_parent_page_id = AllocatePage();
        char* left_bytes = bpm_->FetchPage(cur_parent);
        char* right_bytes = bpm_->FetchPage(right_parent_page_id);
        BNodePage left_parent(left_bytes);
        BNodePage right_parent(right_bytes);
        InitInternalPage(right_bytes);

        const int split_at = degree_ - 1;
        const int64_t promote_key = left_parent.GetKey(degree_, static_cast<std::size_t>(split_at));

        BNodeHeader right_header{};
        right_header.is_leaf = 0;
        right_header.key_count = static_cast<uint16_t>(degree_ - 1);
        right_header.next_leaf_page_id = INVALID_PAGE_ID;
        right_parent.SetHeader(right_header);

        for (int i = 0; i < degree_ - 1; ++i) {
            right_parent.SetKey(degree_, static_cast<std::size_t>(i),
                                left_parent.GetKey(degree_, static_cast<std::size_t>(split_at + 1 + i)));
        }
        for (int i = 0; i < degree_; ++i) {
            right_parent.SetChild(degree_, static_cast<std::size_t>(i),
                                  left_parent.GetChild(degree_, static_cast<std::size_t>(split_at + 1 + i)));
        }

        BNodeHeader left_header = left_parent.GetHeader();
        left_header.key_count = static_cast<uint16_t>(split_at);
        left_parent.SetHeader(left_header);

        bpm_->MarkDirty(cur_parent);
        bpm_->UnpinPage(cur_parent);
        bpm_->MarkDirty(right_parent_page_id);
        bpm_->UnpinPage(right_parent_page_id);

        const page_id_t split_left_page = cur_parent;
        page_id_t target_parent = cur_parent;
        int target_index = cur_index;
        if (cur_index > split_at) {
            target_parent = right_parent_page_id;
            target_index = cur_index - (split_at + 1);
        }

        char* target_bytes = bpm_->FetchPage(target_parent);
        BNodePage target(target_bytes);
        BNodeHeader target_header = target.GetHeader();
        ShiftKeysRight(target, degree_, static_cast<std::size_t>(target_index),
                       static_cast<std::size_t>(target_header.key_count));
        ShiftChildrenRight(target, degree_, static_cast<std::size_t>(target_index),
                           static_cast<std::size_t>(target_header.key_count));
        target.SetKey(degree_, static_cast<std::size_t>(target_index), cur_key);
        target.SetChild(degree_, static_cast<std::size_t>(target_index + 1), cur_right);
        target_header.key_count += 1;
        target.SetHeader(target_header);
        bpm_->MarkDirty(target_parent);
        bpm_->UnpinPage(target_parent);

        page_id_t ancestor = INVALID_PAGE_ID;
        int ancestor_slot = 0;
        FindParentAndSlot(split_left_page, &ancestor, &ancestor_slot);

        cur_key = promote_key;
        cur_right = right_parent_page_id;
        cur_index = ancestor_slot;

        if (ancestor == INVALID_PAGE_ID) {
            const page_id_t new_root_page_id = AllocatePage();
            char* new_root_bytes = bpm_->FetchPage(new_root_page_id);
            InitInternalPage(new_root_bytes);
            BNodePage new_root(new_root_bytes);
            BNodeHeader header{};
            header.is_leaf = 0;
            header.key_count = 1;
            header.next_leaf_page_id = INVALID_PAGE_ID;
            new_root.SetHeader(header);
            new_root.SetKey(degree_, 0, cur_key);
            new_root.SetChild(degree_, 0, split_left_page);
            new_root.SetChild(degree_, 1, cur_right);
            bpm_->MarkDirty(new_root_page_id);
            bpm_->UnpinPage(new_root_page_id);

            root_page_id_ = new_root_page_id;
            SaveMeta();
            return;
        }

        cur_parent = ancestor;
    }
}

void BTree::SplitChild(page_id_t parent_page_id, int child_index) {
    char* parent_bytes = bpm_->FetchPage(parent_page_id);
    BNodePage parent(parent_bytes);
    const page_id_t child_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(child_index));
    bpm_->UnpinPage(parent_page_id);

    char* child_bytes = bpm_->FetchPage(child_page_id);
    BNodePage child(child_bytes);
    BNodeHeader child_header = child.GetHeader();

    const page_id_t new_page_id = AllocatePage();
    char* new_bytes = bpm_->FetchPage(new_page_id);
    BNodePage new_node(new_bytes);

    int64_t promote_key = 0;

    if (child_header.is_leaf != 0) {
        promote_key = child.GetKey(degree_, static_cast<std::size_t>(degree_));
        InitLeafPage(new_bytes, child_header.next_leaf_page_id);

        BNodeHeader new_header = new_node.GetHeader();
        new_header.key_count = static_cast<uint16_t>(degree_ - 1);
        new_node.SetHeader(new_header);

        for (int i = 0; i < degree_ - 1; ++i) {
            const std::size_t src = static_cast<std::size_t>(degree_ + i);
            const std::size_t dst = static_cast<std::size_t>(i);
            new_node.SetKey(degree_, dst, child.GetKey(degree_, src));
            new_node.SetRecord(degree_, dst, child.GetRecord(degree_, src));
        }

        child_header.key_count = static_cast<uint16_t>(degree_);
        child_header.next_leaf_page_id = new_page_id;
        child.SetHeader(child_header);
    } else {
        const int promote_index = degree_ - 1;
        promote_key = child.GetKey(degree_, static_cast<std::size_t>(promote_index));
        InitInternalPage(new_bytes);

        BNodeHeader new_header = new_node.GetHeader();
        new_header.key_count = static_cast<uint16_t>(degree_ - 1);
        new_node.SetHeader(new_header);

        for (int i = 0; i < degree_ - 1; ++i) {
            new_node.SetKey(degree_, static_cast<std::size_t>(i),
                            child.GetKey(degree_, static_cast<std::size_t>(promote_index + 1 + i)));
        }
        for (int i = 0; i < degree_; ++i) {
            new_node.SetChild(degree_, static_cast<std::size_t>(i),
                              child.GetChild(degree_, static_cast<std::size_t>(promote_index + 1 + i)));
        }

        child_header.key_count = static_cast<uint16_t>(degree_ - 1);
        child.SetHeader(child_header);
    }

    bpm_->MarkDirty(child_page_id);
    bpm_->UnpinPage(child_page_id);
    bpm_->MarkDirty(new_page_id);
    bpm_->UnpinPage(new_page_id);

    InsertIntoParent(parent_page_id, child_index, promote_key, new_page_id);
}

void BTree::InsertIntoLeaf(page_id_t leaf_page_id, int64_t key, const RecordId& rid) {
    char* page_bytes = bpm_->FetchPage(leaf_page_id);
    BNodePage leaf(page_bytes);
    BNodeHeader header = leaf.GetHeader();
    const int index = FindKeyIndex(leaf, key);

    ShiftKeysRight(leaf, degree_, static_cast<std::size_t>(index),
                   static_cast<std::size_t>(header.key_count));
    ShiftRecordsRight(leaf, degree_, static_cast<std::size_t>(index),
                      static_cast<std::size_t>(header.key_count));
    leaf.SetKey(degree_, static_cast<std::size_t>(index), key);
    leaf.SetRecord(degree_, static_cast<std::size_t>(index), rid);
    header.key_count += 1;
    leaf.SetHeader(header);

    bpm_->MarkDirty(leaf_page_id);
    bpm_->UnpinPage(leaf_page_id);
}

bool BTree::Insert(int64_t key, const RecordId& rid) {
    char* root_bytes = bpm_->FetchPage(root_page_id_);
    BNodePage root(root_bytes);
    if (NodeIsFull(root, degree_)) {
        bpm_->UnpinPage(root_page_id_);
        const page_id_t new_root_page_id = AllocatePage();
        char* new_root_bytes = bpm_->FetchPage(new_root_page_id);
        InitInternalPage(new_root_bytes);
        BNodePage new_root(new_root_bytes);
        BNodeHeader header{};
        header.is_leaf = 0;
        header.key_count = 0;
        header.next_leaf_page_id = INVALID_PAGE_ID;
        new_root.SetHeader(header);
        new_root.SetChild(degree_, 0, root_page_id_);
        bpm_->MarkDirty(new_root_page_id);
        bpm_->UnpinPage(new_root_page_id);

        root_page_id_ = new_root_page_id;
        SaveMeta();
        SplitChild(root_page_id_, 0);
    } else {
        bpm_->UnpinPage(root_page_id_);
    }

    page_id_t current = root_page_id_;

    while (true) {
        char* page_bytes = bpm_->FetchPage(current);
        BNodePage node(page_bytes);
        BNodeHeader header = node.GetHeader();

        if (header.is_leaf != 0) {
            const int index = FindKeyIndex(node, key);
            if (index < header.key_count &&
                node.GetKey(degree_, static_cast<std::size_t>(index)) == key) {
                bpm_->UnpinPage(current);
                return false;
            }
            bpm_->UnpinPage(current);
            InsertIntoLeaf(current, key, rid);
            return true;
        }

        int child_index = FindChildIndex(node, key);
        page_id_t child_page_id = node.GetChild(degree_, static_cast<std::size_t>(child_index));
        bpm_->UnpinPage(current);

        char* child_bytes = bpm_->FetchPage(child_page_id);
        const bool child_full = NodeIsFull(BNodePage(child_bytes), degree_);
        bpm_->UnpinPage(child_page_id);

        if (child_full) {
            SplitChild(current, child_index);
            char* parent_bytes = bpm_->FetchPage(current);
            BNodePage parent(parent_bytes);
            if (child_index < parent.GetHeader().key_count &&
                key > parent.GetKey(degree_, static_cast<std::size_t>(child_index))) {
                ++child_index;
            }
            child_page_id = parent.GetChild(degree_, static_cast<std::size_t>(child_index));
            bpm_->UnpinPage(current);
        }

        current = child_page_id;
    }
}

int64_t BTree::GetPredecessorKey(page_id_t internal_page_id, int child_index) const {
    page_id_t page_id = INVALID_PAGE_ID;
    {
        char* page_bytes = bpm_->FetchPage(internal_page_id);
        BNodePage node(page_bytes);
        page_id = node.GetChild(degree_, static_cast<std::size_t>(child_index));
        bpm_->UnpinPage(internal_page_id);
    }

    while (true) {
        char* page_bytes = bpm_->FetchPage(page_id);
        BNodePage node(page_bytes);
        BNodeHeader header = node.GetHeader();
        if (header.is_leaf != 0) {
            const int64_t pred =
                node.GetKey(degree_, static_cast<std::size_t>(header.key_count - 1));
            bpm_->UnpinPage(page_id);
            return pred;
        }
        const page_id_t next = node.GetChild(degree_, static_cast<std::size_t>(header.key_count));
        bpm_->UnpinPage(page_id);
        page_id = next;
    }
}

int64_t BTree::GetSuccessorKey(page_id_t internal_page_id, int child_index) const {
    page_id_t page_id = INVALID_PAGE_ID;
    {
        char* page_bytes = bpm_->FetchPage(internal_page_id);
        BNodePage node(page_bytes);
        page_id = node.GetChild(degree_, static_cast<std::size_t>(child_index + 1));
        bpm_->UnpinPage(internal_page_id);
    }

    while (true) {
        char* page_bytes = bpm_->FetchPage(page_id);
        BNodePage node(page_bytes);
        BNodeHeader header = node.GetHeader();
        if (header.is_leaf != 0) {
            const int64_t succ = node.GetKey(degree_, 0);
            bpm_->UnpinPage(page_id);
            return succ;
        }
        const page_id_t next = node.GetChild(degree_, 0);
        bpm_->UnpinPage(page_id);
        page_id = next;
    }
}

void BTree::BorrowFromLeft(page_id_t parent_page_id, int child_index) {
    char* parent_bytes = bpm_->FetchPage(parent_page_id);
    BNodePage parent(parent_bytes);
    const page_id_t child_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(child_index));
    const page_id_t left_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(child_index - 1));

    char* child_bytes = bpm_->FetchPage(child_page_id);
    char* left_bytes = bpm_->FetchPage(left_page_id);
    BNodePage child(child_bytes);
    BNodePage left(left_bytes);
    BNodeHeader child_header = child.GetHeader();
    BNodeHeader left_header = left.GetHeader();

    if (child_header.is_leaf != 0) {
        const int64_t borrowed_key =
            left.GetKey(degree_, static_cast<std::size_t>(left_header.key_count - 1));
        const RecordId borrowed_rid =
            left.GetRecord(degree_, static_cast<std::size_t>(left_header.key_count - 1));
        ShiftKeysRight(child, degree_, 0, static_cast<std::size_t>(child_header.key_count));
        ShiftRecordsRight(child, degree_, 0, static_cast<std::size_t>(child_header.key_count));
        child.SetKey(degree_, 0, borrowed_key);
        child.SetRecord(degree_, 0, borrowed_rid);
        left_header.key_count -= 1;
        parent.SetKey(degree_, static_cast<std::size_t>(child_index - 1), borrowed_key);
    } else {
        ShiftKeysRight(child, degree_, 0, static_cast<std::size_t>(child_header.key_count));
        ShiftChildrenRight(child, degree_, 0, static_cast<std::size_t>(child_header.key_count));
        child.SetKey(degree_, 0, parent.GetKey(degree_, static_cast<std::size_t>(child_index - 1)));
        child.SetChild(degree_, 0, left.GetChild(degree_, static_cast<std::size_t>(left_header.key_count)));
        parent.SetKey(degree_, static_cast<std::size_t>(child_index - 1),
                      left.GetKey(degree_, static_cast<std::size_t>(left_header.key_count - 1)));
        left_header.key_count -= 1;
    }

    child_header.key_count += 1;
    left.SetHeader(left_header);
    child.SetHeader(child_header);

    bpm_->MarkDirty(left_page_id);
    bpm_->MarkDirty(child_page_id);
    bpm_->MarkDirty(parent_page_id);
    bpm_->UnpinPage(left_page_id);
    bpm_->UnpinPage(child_page_id);
    bpm_->UnpinPage(parent_page_id);
}

void BTree::BorrowFromRight(page_id_t parent_page_id, int child_index) {
    char* parent_bytes = bpm_->FetchPage(parent_page_id);
    BNodePage parent(parent_bytes);
    const page_id_t child_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(child_index));
    const page_id_t right_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(child_index + 1));

    char* child_bytes = bpm_->FetchPage(child_page_id);
    char* right_bytes = bpm_->FetchPage(right_page_id);
    BNodePage child(child_bytes);
    BNodePage right(right_bytes);
    BNodeHeader child_header = child.GetHeader();
    BNodeHeader right_header = right.GetHeader();

    if (child_header.is_leaf != 0) {
        const int64_t borrowed_key = right.GetKey(degree_, 0);
        const RecordId borrowed_rid = right.GetRecord(degree_, 0);
        child.SetKey(degree_, static_cast<std::size_t>(child_header.key_count), borrowed_key);
        child.SetRecord(degree_, static_cast<std::size_t>(child_header.key_count), borrowed_rid);
        ShiftKeysLeft(right, degree_, 0, static_cast<std::size_t>(right_header.key_count - 1));
        ShiftRecordsLeft(right, degree_, 0, static_cast<std::size_t>(right_header.key_count - 1));
        right_header.key_count -= 1;
        if (right_header.key_count > 0) {
            parent.SetKey(degree_, static_cast<std::size_t>(child_index),
                          right.GetKey(degree_, 0));
        }
    } else {
        child.SetKey(degree_, static_cast<std::size_t>(child_header.key_count),
                     parent.GetKey(degree_, static_cast<std::size_t>(child_index)));
        child.SetChild(degree_, static_cast<std::size_t>(child_header.key_count + 1),
                       right.GetChild(degree_, 0));
        ShiftKeysLeft(right, degree_, 0, static_cast<std::size_t>(right_header.key_count - 1));
        ShiftChildrenLeft(right, degree_, 0, static_cast<std::size_t>(right_header.key_count));
        right_header.key_count -= 1;
        parent.SetKey(degree_, static_cast<std::size_t>(child_index), right.GetKey(degree_, 0));
    }

    child_header.key_count += 1;
    right.SetHeader(right_header);
    child.SetHeader(child_header);

    bpm_->MarkDirty(right_page_id);
    bpm_->MarkDirty(child_page_id);
    bpm_->MarkDirty(parent_page_id);
    bpm_->UnpinPage(right_page_id);
    bpm_->UnpinPage(child_page_id);
    bpm_->UnpinPage(parent_page_id);
}

void BTree::MergeLeaves(page_id_t parent_page_id, int left_index) {
    char* parent_bytes = bpm_->FetchPage(parent_page_id);
    BNodePage parent(parent_bytes);
    BNodeHeader parent_header = parent.GetHeader();

    const page_id_t left_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(left_index));
    const page_id_t right_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(left_index + 1));

    char* left_bytes = bpm_->FetchPage(left_page_id);
    char* right_bytes = bpm_->FetchPage(right_page_id);
    BNodePage left(left_bytes);
    BNodePage right(right_bytes);
    BNodeHeader left_header = left.GetHeader();
    BNodeHeader right_header = right.GetHeader();

    const std::size_t left_count = left_header.key_count;
    for (int i = 0; i < right_header.key_count; ++i) {
        left.SetKey(degree_, left_count + static_cast<std::size_t>(i),
                    right.GetKey(degree_, static_cast<std::size_t>(i)));
        left.SetRecord(degree_, left_count + static_cast<std::size_t>(i),
                       right.GetRecord(degree_, static_cast<std::size_t>(i)));
    }

    left_header.key_count =
        static_cast<uint16_t>(left_count + static_cast<std::size_t>(right_header.key_count));
    left_header.next_leaf_page_id = right_header.next_leaf_page_id;
    left.SetHeader(left_header);

    ShiftKeysLeft(parent, degree_, static_cast<std::size_t>(left_index),
                  static_cast<std::size_t>(parent_header.key_count - 1));
    ShiftChildrenLeft(parent, degree_, static_cast<std::size_t>(left_index + 1),
                      static_cast<std::size_t>(parent_header.key_count - 1));
    parent_header.key_count -= 1;
    parent.SetHeader(parent_header);

    bpm_->MarkDirty(left_page_id);
    bpm_->MarkDirty(parent_page_id);
    bpm_->UnpinPage(left_page_id);
    bpm_->UnpinPage(right_page_id);
    bpm_->UnpinPage(parent_page_id);
}

void BTree::MergeChildren(page_id_t parent_page_id, int left_index) {
    char* parent_bytes = bpm_->FetchPage(parent_page_id);
    BNodePage parent(parent_bytes);
    BNodeHeader parent_header = parent.GetHeader();

    const page_id_t left_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(left_index));
    const page_id_t right_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(left_index + 1));

    char* left_bytes = bpm_->FetchPage(left_page_id);
    char* right_bytes = bpm_->FetchPage(right_page_id);
    BNodePage left(left_bytes);
    BNodePage right(right_bytes);
    BNodeHeader left_header = left.GetHeader();
    BNodeHeader right_header = right.GetHeader();

    left.SetKey(degree_, static_cast<std::size_t>(left_header.key_count),
                parent.GetKey(degree_, static_cast<std::size_t>(left_index)));

    const std::size_t left_count = left_header.key_count + 1;
    for (int i = 0; i < right_header.key_count; ++i) {
        left.SetKey(degree_, left_count + static_cast<std::size_t>(i),
                    right.GetKey(degree_, static_cast<std::size_t>(i)));
    }
    for (int i = 0; i <= right_header.key_count; ++i) {
        left.SetChild(degree_, left_count + static_cast<std::size_t>(i),
                      right.GetChild(degree_, static_cast<std::size_t>(i)));
    }

    left_header.key_count = static_cast<uint16_t>(
        left_count + static_cast<std::size_t>(right_header.key_count));
    left.SetHeader(left_header);

    ShiftKeysLeft(parent, degree_, static_cast<std::size_t>(left_index),
                  static_cast<std::size_t>(parent_header.key_count - 1));
    ShiftChildrenLeft(parent, degree_, static_cast<std::size_t>(left_index + 1),
                      static_cast<std::size_t>(parent_header.key_count - 1));
    parent_header.key_count -= 1;
    parent.SetHeader(parent_header);

    bpm_->MarkDirty(left_page_id);
    bpm_->MarkDirty(parent_page_id);
    bpm_->UnpinPage(left_page_id);
    bpm_->UnpinPage(right_page_id);
    bpm_->UnpinPage(parent_page_id);
}

void BTree::EnsureLeafCapacity(page_id_t parent_page_id, int child_index) {
    char* parent_bytes = bpm_->FetchPage(parent_page_id);
    BNodePage parent(parent_bytes);
    BNodeHeader parent_header = parent.GetHeader();
    const page_id_t child_page_id =
        parent.GetChild(degree_, static_cast<std::size_t>(child_index));

    char* child_bytes = bpm_->FetchPage(child_page_id);
    BNodePage child(child_bytes);
    BNodeHeader child_header = child.GetHeader();
    const bool is_leaf = child_header.is_leaf != 0;
    bpm_->UnpinPage(child_page_id);

    if (static_cast<int>(child_header.key_count) >= degree_ - 1) {
        bpm_->UnpinPage(parent_page_id);
        return;
    }

    if (child_index > 0) {
        const page_id_t left_id =
            parent.GetChild(degree_, static_cast<std::size_t>(child_index - 1));
        char* left_bytes = bpm_->FetchPage(left_id);
        const int left_keys = BNodePage(left_bytes).GetHeader().key_count;
        bpm_->UnpinPage(left_id);
        if (left_keys >= degree_) {
            bpm_->UnpinPage(parent_page_id);
            BorrowFromLeft(parent_page_id, child_index);
            return;
        }
    }

    if (child_index < parent_header.key_count) {
        const page_id_t right_id =
            parent.GetChild(degree_, static_cast<std::size_t>(child_index + 1));
        char* right_bytes = bpm_->FetchPage(right_id);
        const int right_keys = BNodePage(right_bytes).GetHeader().key_count;
        bpm_->UnpinPage(right_id);
        if (right_keys >= degree_) {
            bpm_->UnpinPage(parent_page_id);
            BorrowFromRight(parent_page_id, child_index);
            return;
        }
    }

    bpm_->UnpinPage(parent_page_id);
    if (is_leaf) {
        if (child_index < parent_header.key_count) {
            MergeLeaves(parent_page_id, child_index);
        } else {
            MergeLeaves(parent_page_id, child_index - 1);
        }
    } else if (child_index < parent_header.key_count) {
        MergeChildren(parent_page_id, child_index);
    } else {
        MergeChildren(parent_page_id, child_index - 1);
    }
}

void BTree::ShrinkRootIfNeeded() {
    char* root_bytes = bpm_->FetchPage(root_page_id_);
    BNodePage root(root_bytes);
    BNodeHeader root_header = root.GetHeader();

    if (root_header.is_leaf != 0 || root_header.key_count != 0) {
        bpm_->UnpinPage(root_page_id_);
        return;
    }

    const page_id_t old_root = root_page_id_;
    const page_id_t new_root = root.GetChild(degree_, 0);
    bpm_->UnpinPage(old_root);

    root_page_id_ = new_root;
    SaveMeta();
}

void BTree::RemoveFromLeaf(page_id_t leaf_page_id, int key_index) {
    char* leaf_bytes = bpm_->FetchPage(leaf_page_id);
    BNodePage leaf(leaf_bytes);
    BNodeHeader header = leaf.GetHeader();

    ShiftKeysLeft(leaf, degree_, static_cast<std::size_t>(key_index),
                  static_cast<std::size_t>(header.key_count - 1));
    ShiftRecordsLeft(leaf, degree_, static_cast<std::size_t>(key_index),
                     static_cast<std::size_t>(header.key_count - 1));
    header.key_count -= 1;
    leaf.SetHeader(header);

    bpm_->MarkDirty(leaf_page_id);
    bpm_->UnpinPage(leaf_page_id);
}

void BTree::DeleteFromLeaf(page_id_t leaf_page_id, int64_t key) {
    std::vector<std::pair<page_id_t, int>> path;

    if (leaf_page_id != root_page_id_) {
        page_id_t current = root_page_id_;
        while (current != leaf_page_id) {
            char* page_bytes = bpm_->FetchPage(current);
            BNodePage node(page_bytes);
            const int next_index = FindChildIndex(node, key);
            path.emplace_back(current, next_index);
            current = node.GetChild(degree_, static_cast<std::size_t>(next_index));
            bpm_->UnpinPage(path.back().first);
        }
    }

    char* leaf_bytes = bpm_->FetchPage(leaf_page_id);
    BNodePage leaf(leaf_bytes);
    const int index = FindKeyIndex(leaf, key);
    bpm_->UnpinPage(leaf_page_id);

    RemoveFromLeaf(leaf_page_id, index);

    if (index == 0 && !path.empty()) {
        char* updated_leaf_bytes = bpm_->FetchPage(leaf_page_id);
        BNodePage updated_leaf(updated_leaf_bytes);
        const BNodeHeader updated_header = updated_leaf.GetHeader();
        if (updated_header.key_count > 0) {
            const int64_t new_min = updated_leaf.GetKey(degree_, 0);
            bpm_->UnpinPage(leaf_page_id);

            if (path.back().second > 0) {
                const page_id_t parent_id = path.back().first;
                char* parent_bytes = bpm_->FetchPage(parent_id);
                BNodePage parent(parent_bytes);
                parent.SetKey(degree_, static_cast<std::size_t>(path.back().second - 1), new_min);
                bpm_->MarkDirty(parent_id);
                bpm_->UnpinPage(parent_id);
            }
        } else {
            bpm_->UnpinPage(leaf_page_id);
        }
    }

    if (leaf_page_id == root_page_id_) {
        return;
    }

    leaf_bytes = bpm_->FetchPage(leaf_page_id);
    const BNodeHeader after_header = BNodePage(leaf_bytes).GetHeader();
    bpm_->UnpinPage(leaf_page_id);

    if (static_cast<int>(after_header.key_count) >= degree_ - 1) {
        return;
    }

    EnsureLeafCapacity(path.back().first, path.back().second);
}

bool BTree::Remove(int64_t key) {
    const page_id_t leaf_page_id = FindLeafPage(key);
    char* leaf_bytes = bpm_->FetchPage(leaf_page_id);
    BNodePage leaf(leaf_bytes);
    const int index = FindKeyIndex(leaf, key);
    const BNodeHeader header = leaf.GetHeader();
    const bool found =
        index < header.key_count &&
        leaf.GetKey(degree_, static_cast<std::size_t>(index)) == key;
    bpm_->UnpinPage(leaf_page_id);

    if (!found) {
        return false;
    }

    DeleteFromLeaf(leaf_page_id, key);
    ShrinkRootIfNeeded();
    return true;
}

int BTree::Height() const {
    int height = 0;
    page_id_t page_id = root_page_id_;
    while (page_id != INVALID_PAGE_ID) {
        ++height;
        char* page_bytes = bpm_->FetchPage(page_id);
        BNodePage node(page_bytes);
        BNodeHeader header = node.GetHeader();
        if (header.is_leaf != 0) {
            bpm_->UnpinPage(page_id);
            break;
        }
        const page_id_t next = node.GetChild(degree_, 0);
        bpm_->UnpinPage(page_id);
        page_id = next;
    }
    return height;
}

std::vector<std::pair<int64_t, RecordId>> BTree::RangeSearch(int64_t low_key,
                                                             int64_t high_key) const {
    if (low_key > high_key) {
        return {};
    }

    std::vector<std::pair<int64_t, RecordId>> results;
    page_id_t leaf_page_id = root_page_id_;
    while (leaf_page_id != INVALID_PAGE_ID) {
        char* page_bytes = bpm_->FetchPage(leaf_page_id);
        BNodePage node(page_bytes);
        BNodeHeader header = node.GetHeader();
        if (header.is_leaf != 0) {
            bpm_->UnpinPage(leaf_page_id);
            break;
        }
        const page_id_t next = node.GetChild(degree_, 0);
        bpm_->UnpinPage(leaf_page_id);
        leaf_page_id = next;
    }

    while (leaf_page_id != INVALID_PAGE_ID) {
        char* page_bytes = bpm_->FetchPage(leaf_page_id);
        BNodePage leaf(page_bytes);
        BNodeHeader header = leaf.GetHeader();

        for (int i = 0; i < header.key_count; ++i) {
            const int64_t key = leaf.GetKey(degree_, static_cast<std::size_t>(i));
            if (key > high_key) {
                bpm_->UnpinPage(leaf_page_id);
                return results;
            }
            if (key >= low_key) {
                results.emplace_back(key, leaf.GetRecord(degree_, static_cast<std::size_t>(i)));
            }
        }

        const page_id_t next_leaf = header.next_leaf_page_id;
        bpm_->UnpinPage(leaf_page_id);
        leaf_page_id = next_leaf;
    }

    return results;
}

}  // namespace minidb
