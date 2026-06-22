#pragma once

#include "common/types.h"
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace minidb {

// bplustree — in-memory b+ tree for primary-key indexing
// this b+ tree is built over the lsm storage engine.  pointers to records
// are stored in leaf nodes.  the tree itself lives in memory (for fast
// access), but leaf values reference keys in the lsm engine.
//
// order `fanout` controls how many children each internal node can have.
// default of 4 keeps things simple and debuggable.

template <typename KeyType = Key, size_t FANOUT = 4>
class BPlusTree {
public:
    BPlusTree() { _root = std::make_unique<LeafNode>(); }

    // --- operations ---------------------------------------------------------
    bool insert(const KeyType& key, const Record& record);
    bool search(const KeyType& key, Record& out) const;
    bool remove(const KeyType& key);

    // range scan: find all keys in [low, high], inclusive.
    std::vector<std::pair<KeyType, Record>> range_scan(
        const KeyType& low, const KeyType& high) const;

    // exact-match search returning a record (convenience).
    Record get(const KeyType& key, bool& found) const;

    size_t size() const { return _size; }
    bool   empty() const { return _size == 0; }

private:
    struct Node {
        bool is_leaf = false;
        virtual ~Node() = default;
    };

    struct InternalNode : Node {
        InternalNode() { Node::is_leaf = false; }
        std::vector<KeyType>              keys;       // fanout-1 keys max
        std::vector<std::unique_ptr<Node>> children;  // fanout children max
    };

    struct LeafNode : Node {
        LeafNode() { Node::is_leaf = true; _next = nullptr; }
        std::vector<KeyType> keys;
        std::vector<Record>  records;
        LeafNode*            _next;  // sibling pointer for range scans
    };

    // internal helpers
    void insert_internal(const KeyType& key, const Record& record,
                         Node* node, std::unique_ptr<Node>& new_child,
                         KeyType& promote_key);
    bool remove_internal(const KeyType& key, Node* node);
    LeafNode* find_leaf(const KeyType& key) const;

    std::unique_ptr<Node> _root;
    size_t                _size = 0;
};

// implementation

template <typename KeyType, size_t FANOUT>
typename BPlusTree<KeyType, FANOUT>::LeafNode*
BPlusTree<KeyType, FANOUT>::find_leaf(const KeyType& key) const {
    Node* node = _root.get();
    while (!node->is_leaf) {
        auto* internal = static_cast<InternalNode*>(node);
        size_t i = 0;
        while (i < internal->keys.size() && key >= internal->keys[i]) ++i;
        node = internal->children[i].get();
    }
    return static_cast<LeafNode*>(node);
}

template <typename KeyType, size_t FANOUT>
bool BPlusTree<KeyType, FANOUT>::search(const KeyType& key, Record& out) const {
    auto* leaf = find_leaf(key);
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
        if (leaf->keys[i] == key) {
            out = leaf->records[i];
            return true;
        }
    }
    return false;
}

template <typename KeyType, size_t FANOUT>
Record BPlusTree<KeyType, FANOUT>::get(const KeyType& key, bool& found) const {
    Record rec;
    found = search(key, rec);
    return rec;
}

template <typename KeyType, size_t FANOUT>
void BPlusTree<KeyType, FANOUT>::insert_internal(
    const KeyType& key, const Record& record,
    Node* node, std::unique_ptr<Node>& new_child, KeyType& promote_key) {

    if (node->is_leaf) {
        auto* leaf = static_cast<LeafNode*>(node);
        // find insertion point
        auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
        size_t pos = it - leaf->keys.begin();

        // update existing key
        if (pos < leaf->keys.size() && leaf->keys[pos] == key) {
            leaf->records[pos] = record;
            new_child = nullptr;
            return;
        }

        leaf->keys.insert(it, key);
        leaf->records.insert(leaf->records.begin() + pos, record);
        ++_size;

        // check for split
        if (leaf->keys.size() <= FANOUT) {
            new_child = nullptr;
            return;
        }

        // split leaf
        size_t mid = leaf->keys.size() / 2;
        auto new_leaf = std::make_unique<LeafNode>();
        new_leaf->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
        new_leaf->records.assign(leaf->records.begin() + mid, leaf->records.end());
        new_leaf->_next = leaf->_next;
        leaf->_next = new_leaf.get();

        leaf->keys.resize(mid);
        leaf->records.resize(mid);

        promote_key = new_leaf->keys[0];
        new_child = std::move(new_leaf);
        return;
    }

    // internal node
    auto* internal = static_cast<InternalNode*>(node);
    size_t i = 0;
    while (i < internal->keys.size() && key >= internal->keys[i]) ++i;

    std::unique_ptr<Node> child_new_node;
    KeyType child_promote_key;
    insert_internal(key, record, internal->children[i].get(),
                    child_new_node, child_promote_key);

    if (!child_new_node) {
        new_child = nullptr;
        return;
    }

    // insert promoted key and child
    internal->keys.insert(internal->keys.begin() + i, child_promote_key);
    internal->children.insert(internal->children.begin() + i + 1,
                              std::move(child_new_node));

    if (internal->keys.size() < FANOUT) {
        new_child = nullptr;
        return;
    }

    // split internal node
    size_t mid = internal->keys.size() / 2;
    promote_key = internal->keys[mid];

    auto new_internal = std::make_unique<InternalNode>();
    new_internal->keys.assign(internal->keys.begin() + mid + 1, internal->keys.end());
    new_internal->children.assign(
        std::make_move_iterator(internal->children.begin() + mid + 1),
        std::make_move_iterator(internal->children.end()));

    internal->keys.resize(mid);
    internal->children.resize(mid + 1);
    new_child = std::move(new_internal);
}

template <typename KeyType, size_t FANOUT>
bool BPlusTree<KeyType, FANOUT>::insert(const KeyType& key, const Record& record) {
    std::unique_ptr<Node> new_child;
    KeyType promote_key;
    insert_internal(key, record, _root.get(), new_child, promote_key);

    if (new_child) {
        // root was split — create new root
        auto new_root = std::make_unique<InternalNode>();
        new_root->keys.push_back(promote_key);
        new_root->children.push_back(std::move(_root));
        new_root->children.push_back(std::move(new_child));
        _root = std::move(new_root);
    }
    return true;
}

template <typename KeyType, size_t FANOUT>
bool BPlusTree<KeyType, FANOUT>::remove(const KeyType& key) {
    auto* leaf = find_leaf(key);
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
        if (leaf->keys[i] == key) {
            leaf->keys.erase(leaf->keys.begin() + i);
            leaf->records.erase(leaf->records.begin() + i);
            --_size;
            return true;
        }
    }
    return false;
}

template <typename KeyType, size_t FANOUT>
std::vector<std::pair<KeyType, Record>>
BPlusTree<KeyType, FANOUT>::range_scan(
    const KeyType& low, const KeyType& high) const {

    std::vector<std::pair<KeyType, Record>> results;
    auto* leaf = find_leaf(low);

    while (leaf) {
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] > high) return results;
            if (leaf->keys[i] >= low) {
                results.emplace_back(leaf->keys[i], leaf->records[i]);
            }
        }
        leaf = leaf->_next;
    }
    return results;
}

} // namespace minidb
