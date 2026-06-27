#pragma once
#include <vector>
#include <optional>
#include <functional>

static constexpr int BTREE_MAX_KEYS = 4;

struct BNode {
    bool             is_leaf;
    std::vector<int> keys;
    std::vector<int> vals;
    std::vector<BNode*> children;
    BNode*           next = nullptr;

    explicit BNode(bool leaf) : is_leaf(leaf) {}
    ~BNode() {
        if (!is_leaf) for (auto* c : children) delete c;
    }
};

class BTree {
public:
    BTree();
    ~BTree();

    std::optional<int> search(int key) const;
    void               insert(int key, int row_id);
    void               remove(int key);
    void               range_scan(int lo, int hi,
                                  std::function<void(int,int)> cb) const;

private:
    BNode* root_;

    BNode* find_leaf(int key) const;

    std::optional<std::pair<int,BNode*>> insert_rec(BNode* node, int key, int row_id);

    std::pair<int,BNode*> split_leaf(BNode* leaf);
    std::pair<int,BNode*> split_internal(BNode* node);
};
