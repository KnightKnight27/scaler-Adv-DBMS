#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/types.h"
#include "storage/buffer_pool.h"
#include "storage/page_manager.h"

namespace minidb {

class BPlusTree {
public:
    static constexpr int kOrder = 32;

    BPlusTree(PageManager* page_manager, BufferPool* buffer_pool, int root_page_id);

    int root_page_id() const { return root_page_id_; }

    std::optional<Rid> Search(const Value& key) const;
    std::vector<Rid> SearchRange(const Value& low, const Value& high) const;
    void Insert(const Value& key, const Rid& rid);
    bool Remove(const Value& key);

private:
    struct SplitResult {
        Value key;
        int right_page;
    };

    struct NodeView {
        bool leaf = true;
        int num_keys = 0;
        std::vector<Value> keys;
        std::vector<int> children;
        std::vector<Rid> rids;
        int next_leaf = INVALID_PAGE_ID;
    };

    PageManager* page_manager_;
    BufferPool* buffer_pool_;
    int root_page_id_;

    NodeView ReadNode(int page_id) const;
    void WriteNode(int page_id, const NodeView& node);
    int AllocateNode(bool leaf);
    std::optional<Rid> SearchNode(int page_id, const Value& key) const;
    int FindChild(const NodeView& node, const Value& key) const;
    std::optional<SplitResult> InsertRecursive(int page_id, const Value& key, const Rid& rid);
    std::optional<SplitResult> SplitLeafNode(int page_id, NodeView node);
    std::optional<SplitResult> SplitInternalNode(int page_id, NodeView node);
    void InsertIntoInternal(NodeView& node, const Value& key, int right_child);
};

}  // namespace minidb
