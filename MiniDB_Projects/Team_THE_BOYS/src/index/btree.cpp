#include "index/btree.h"

#include <algorithm>
#include <cstring>

namespace minidb {

namespace {
constexpr int kHeaderSize = 16;

void WriteValue(std::vector<char>& buf, std::size_t& pos, const Value& v) {
    if (v.type == ValueType::INT) {
        buf[pos++] = 1;
        int64_t val = std::get<int64_t>(v.data);
        std::memcpy(buf.data() + pos, &val, sizeof(val));
        pos += sizeof(val);
    } else if (v.type == ValueType::STRING) {
        buf[pos++] = 2;
        const auto& s = std::get<std::string>(v.data);
        uint16_t len = static_cast<uint16_t>(s.size());
        std::memcpy(buf.data() + pos, &len, sizeof(len));
        pos += sizeof(len);
        std::memcpy(buf.data() + pos, s.data(), s.size());
        pos += s.size();
    } else {
        buf[pos++] = 0;
    }
}

Value ReadValue(const char* data, std::size_t& pos) {
    uint8_t tag = static_cast<uint8_t>(data[pos++]);
    if (tag == 0) return Value::Null();
    if (tag == 1) {
        int64_t val = 0;
        std::memcpy(&val, data + pos, sizeof(val));
        pos += sizeof(val);
        return Value::Int(val);
    }
    uint16_t len = 0;
    std::memcpy(&len, data + pos, sizeof(len));
    pos += sizeof(len);
    std::string s(data + pos, data + pos + len);
    pos += len;
    return Value::Str(std::move(s));
}

bool InRange(const Value& key, const Value& low, const Value& high) {
    return !(key < low) && !(high < key);
}
}  // namespace

BPlusTree::BPlusTree(PageManager* page_manager, BufferPool* buffer_pool, int root_page_id)
    : page_manager_(page_manager),
      buffer_pool_(buffer_pool),
      root_page_id_(root_page_id) {}

BPlusTree::NodeView BPlusTree::ReadNode(int page_id) const {
    Page* page = buffer_pool_->FetchPage(page_id);
    const char* data = page->Data();
    NodeView node;
    node.leaf = data[0] != 0;
    node.num_keys = static_cast<uint8_t>(data[1]);
    if (node.leaf) {
        std::memcpy(&node.next_leaf, data + 8, sizeof(int));
    }
    std::size_t pos = kHeaderSize;
    for (int i = 0; i < node.num_keys; ++i) {
        node.keys.push_back(ReadValue(data, pos));
        if (node.leaf) {
            Rid rid;
            std::memcpy(&rid.page_id, data + pos, sizeof(int));
            pos += sizeof(int);
            std::memcpy(&rid.slot_id, data + pos, sizeof(int));
            pos += sizeof(int);
            node.rids.push_back(rid);
        } else {
            int child = 0;
            std::memcpy(&child, data + pos, sizeof(int));
            pos += sizeof(int);
            node.children.push_back(child);
        }
    }
    if (!node.leaf && node.num_keys >= 0) {
        int last_child = 0;
        std::memcpy(&last_child, data + pos, sizeof(int));
        node.children.push_back(last_child);
    }
    buffer_pool_->UnpinPage(page_id, false);
    return node;
}

void BPlusTree::WriteNode(int page_id, const NodeView& node) {
    Page* page = buffer_pool_->FetchPage(page_id);
    std::vector<char> buf(PAGE_SIZE, '\0');
    buf[0] = node.leaf ? 1 : 0;
    buf[1] = static_cast<char>(node.num_keys);
    if (node.leaf) {
        std::memcpy(buf.data() + 8, &node.next_leaf, sizeof(int));
    }
    std::size_t pos = kHeaderSize;
    for (int i = 0; i < node.num_keys; ++i) {
        WriteValue(buf, pos, node.keys[i]);
        if (node.leaf) {
            std::memcpy(buf.data() + pos, &node.rids[i].page_id, sizeof(int));
            pos += sizeof(int);
            std::memcpy(buf.data() + pos, &node.rids[i].slot_id, sizeof(int));
            pos += sizeof(int);
        } else {
            std::memcpy(buf.data() + pos, &node.children[i], sizeof(int));
            pos += sizeof(int);
        }
    }
    if (!node.leaf && !node.children.empty()) {
        int last = node.children.back();
        std::memcpy(buf.data() + pos, &last, sizeof(int));
    }
    std::memcpy(page->MutableData(), buf.data(), PAGE_SIZE);
    buffer_pool_->UnpinPage(page_id, true);
}

int BPlusTree::AllocateNode(bool leaf) {
    int page_id = page_manager_->AllocatePage();
    NodeView node;
    node.leaf = leaf;
    node.num_keys = 0;
    node.next_leaf = INVALID_PAGE_ID;
    WriteNode(page_id, node);
    return page_id;
}

int BPlusTree::FindChild(const NodeView& node, const Value& key) const {
    int idx = 0;
    for (int i = 0; i < node.num_keys; ++i) {
        if (key < node.keys[i]) break;
        idx = i + 1;
    }
    return idx;
}

std::optional<Rid> BPlusTree::Search(const Value& key) const {
    return SearchNode(root_page_id_, key);
}

std::optional<Rid> BPlusTree::SearchNode(int page_id, const Value& key) const {
    NodeView node = ReadNode(page_id);
    if (node.leaf) {
        for (int i = 0; i < node.num_keys; ++i) {
            if (node.keys[i] == key) return node.rids[i];
        }
        return std::nullopt;
    }
    return SearchNode(node.children[FindChild(node, key)], key);
}

std::vector<Rid> BPlusTree::SearchRange(const Value& low, const Value& high) const {
    std::vector<Rid> result;
    int page_id = root_page_id_;
    while (true) {
        NodeView node = ReadNode(page_id);
        if (node.leaf) break;
        page_id = node.children.front();
    }
    while (page_id != INVALID_PAGE_ID) {
        NodeView node = ReadNode(page_id);
        for (int i = 0; i < node.num_keys; ++i) {
            if (InRange(node.keys[i], low, high)) result.push_back(node.rids[i]);
        }
        page_id = node.next_leaf;
    }
    return result;
}

void BPlusTree::InsertIntoInternal(NodeView& node, const Value& key, int right_child) {
    auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key,
                               [](const Value& a, const Value& b) { return a < b; });
    int idx = static_cast<int>(std::distance(node.keys.begin(), it));
    node.keys.insert(node.keys.begin() + idx, key);
    node.children.insert(node.children.begin() + idx + 1, right_child);
    node.num_keys++;
}

std::optional<BPlusTree::SplitResult> BPlusTree::SplitLeafNode(int page_id, NodeView node) {
    int mid = node.num_keys / 2;
    int new_page = AllocateNode(true);
    NodeView right;
    right.leaf = true;
    right.num_keys = node.num_keys - mid;
    right.keys.assign(node.keys.begin() + mid, node.keys.end());
    right.rids.assign(node.rids.begin() + mid, node.rids.end());
    right.next_leaf = node.next_leaf;
    node.num_keys = mid;
    node.next_leaf = new_page;
    WriteNode(page_id, node);
    WriteNode(new_page, right);
    return SplitResult{right.keys.front(), new_page};
}

std::optional<BPlusTree::SplitResult> BPlusTree::SplitInternalNode(int page_id, NodeView node) {
    int mid = node.num_keys / 2;
    Value promote = node.keys[mid];
    int new_page = AllocateNode(false);
    NodeView right;
    right.leaf = false;
    right.num_keys = node.num_keys - mid - 1;
    right.keys.assign(node.keys.begin() + mid + 1, node.keys.end());
    right.children.assign(node.children.begin() + mid + 1, node.children.end());
    node.num_keys = mid;
    node.keys.resize(mid);
    node.children.resize(static_cast<std::size_t>(mid) + 1);
    WriteNode(page_id, node);
    WriteNode(new_page, right);
    return SplitResult{promote, new_page};
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertRecursive(int page_id, const Value& key,
                                                                 const Rid& rid) {
    NodeView node = ReadNode(page_id);
    if (node.leaf) {
        auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key,
                                   [](const Value& a, const Value& b) { return a < b; });
        int idx = static_cast<int>(std::distance(node.keys.begin(), it));
        node.keys.insert(node.keys.begin() + idx, key);
        node.rids.insert(node.rids.begin() + idx, rid);
        node.num_keys++;
        if (node.num_keys < kOrder) {
            WriteNode(page_id, node);
            return std::nullopt;
        }
        return SplitLeafNode(page_id, node);
    }

    int child_idx = FindChild(node, key);
    int child = node.children[child_idx];
    auto split = InsertRecursive(child, key, rid);
    if (!split) {
        return std::nullopt;
    }

    node.keys.insert(node.keys.begin() + child_idx, split->key);
    node.children.insert(node.children.begin() + child_idx + 1, split->right_page);
    node.num_keys++;

    if (node.num_keys < kOrder) {
        WriteNode(page_id, node);
        return std::nullopt;
    }
    return SplitInternalNode(page_id, node);
}

void BPlusTree::Insert(const Value& key, const Rid& rid) {
    auto split = InsertRecursive(root_page_id_, key, rid);
    if (!split) return;
    int new_root = AllocateNode(false);
    NodeView root;
    root.leaf = false;
    root.num_keys = 1;
    root.keys = {split->key};
    root.children = {root_page_id_, split->right_page};
    WriteNode(new_root, root);
    root_page_id_ = new_root;
}

bool BPlusTree::Remove(const Value& key) {
    int page_id = root_page_id_;
    while (true) {
        NodeView node = ReadNode(page_id);
        if (node.leaf) {
            for (int i = 0; i < node.num_keys; ++i) {
                if (node.keys[i] == key) {
                    node.keys.erase(node.keys.begin() + i);
                    node.rids.erase(node.rids.begin() + i);
                    node.num_keys--;
                    WriteNode(page_id, node);
                    return true;
                }
            }
            return false;
        }
        page_id = node.children[FindChild(node, key)];
    }
}

}  // namespace minidb
