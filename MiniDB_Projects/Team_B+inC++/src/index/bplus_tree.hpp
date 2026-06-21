#pragma once

#include <optional>
#include <vector>

#include "../common/types.hpp"

// in-memory B+ tree: key -> RowID, leaves linked
class BPlusTree {
public:
    using Key = int;

    BPlusTree() = default;
    ~BPlusTree();
    BPlusTree(const BPlusTree&) = delete;             // owns raw node ptrs
    BPlusTree& operator=(const BPlusTree&) = delete;

    // insert or overwrite (pks unique)
    void insert(Key key, RowID rid);

    std::optional<RowID> search(Key key) const;

    // RowIDs with key in [low, high], ascending
    std::vector<RowID> range(Key low, Key high) const;

    // false if absent. lazy: no merge/borrow
    bool remove(Key key);

private:
    struct Node {
        bool                 leaf;
        std::vector<Key>     keys;
        std::vector<Node*>   children;  // internal: size == keys.size() + 1
        std::vector<RowID>   values;    // leaf: size == keys.size()
        Node*                next = nullptr;  // leaf: next leaf in chain
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
    };

    // node split absorbed by parent
    struct Split {
        Key   key;     // separator pushed up
        Node* right;
    };

    Node* root_ = nullptr;
    static constexpr int ORDER = 4;  // max keys per node

    std::optional<Split> insert_rec(Node* node, Key key, RowID rid);
    Node* find_leaf(Key key) const;
    static void destroy(Node* node);
};
