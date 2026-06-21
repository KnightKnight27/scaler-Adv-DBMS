#include "bplus_tree.hpp"

#include <algorithm>

BPlusTree::~BPlusTree() { destroy(root_); }

void BPlusTree::destroy(Node* node) {
    if (!node) return;
    if (!node->leaf)
        for (Node* c : node->children) destroy(c);
    delete node;  // leaves are freed here too (reached via their parent's children)
}

// Descend from the root to the leaf whose key range covers `key`.
// Routing rule: child[i] holds keys in [keys[i-1], keys[i]); so we take the
// first child index i where key < keys[i], i.e. advance while key >= keys[i].
BPlusTree::Node* BPlusTree::find_leaf(Key key) const {
    Node* n = root_;
    while (n && !n->leaf) {
        std::size_t i = 0;
        while (i < n->keys.size() && key >= n->keys[i]) ++i;
        n = n->children[i];
    }
    return n;
}

std::optional<RowID> BPlusTree::search(Key key) const {
    Node* leaf = find_leaf(key);
    if (!leaf) return std::nullopt;
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key)
        return leaf->values[static_cast<std::size_t>(it - leaf->keys.begin())];
    return std::nullopt;
}

void BPlusTree::insert(Key key, RowID rid) {
    if (!root_) {
        root_ = new Node(true);
        root_->keys.push_back(key);
        root_->values.push_back(rid);
        return;
    }
    std::optional<Split> split = insert_rec(root_, key, rid);
    if (split) {
        // Root split: grow the tree by one level.
        Node* new_root = new Node(false);
        new_root->keys.push_back(split->key);
        new_root->children.push_back(root_);
        new_root->children.push_back(split->right);
        root_ = new_root;
    }
}

std::optional<BPlusTree::Split> BPlusTree::insert_rec(Node* node, Key key, RowID rid) {
    if (node->leaf) {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        std::size_t pos = static_cast<std::size_t>(it - node->keys.begin());
        if (it != node->keys.end() && *it == key) {  // upsert: key already present
            node->values[pos] = rid;
            return std::nullopt;
        }
        node->keys.insert(it, key);
        node->values.insert(node->values.begin() + pos, rid);
        if (static_cast<int>(node->keys.size()) <= ORDER) return std::nullopt;

        // Split leaf: right sibling takes the upper half; chain the leaves.
        std::size_t mid = node->keys.size() / 2;
        Node* right = new Node(true);
        right->keys.assign(node->keys.begin() + mid, node->keys.end());
        right->values.assign(node->values.begin() + mid, node->values.end());
        node->keys.resize(mid);
        node->values.resize(mid);
        right->next = node->next;
        node->next = right;
        return Split{right->keys.front(), right};  // copy first key of right up
    }

    // Internal node: route to the right child, then absorb any split it returns.
    std::size_t i = 0;
    while (i < node->keys.size() && key >= node->keys[i]) ++i;
    std::optional<Split> child_split = insert_rec(node->children[i], key, rid);
    if (!child_split) return std::nullopt;

    node->keys.insert(node->keys.begin() + i, child_split->key);
    node->children.insert(node->children.begin() + i + 1, child_split->right);
    if (static_cast<int>(node->keys.size()) <= ORDER) return std::nullopt;

    // Split internal: the median key moves UP (removed from this level).
    std::size_t mid = node->keys.size() / 2;
    Key up = node->keys[mid];
    Node* right = new Node(false);
    right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    right->children.assign(node->children.begin() + mid + 1, node->children.end());
    node->keys.resize(mid);
    node->children.resize(mid + 1);
    return Split{up, right};
}

std::vector<RowID> BPlusTree::range(Key low, Key high) const {
    std::vector<RowID> out;
    Node* n = find_leaf(low);
    while (n) {
        for (std::size_t i = 0; i < n->keys.size(); ++i) {
            if (n->keys[i] > high) return out;      // sorted + chained: we're done
            if (n->keys[i] >= low) out.push_back(n->values[i]);
        }
        n = n->next;
    }
    return out;
}

bool BPlusTree::remove(Key key) {
    Node* leaf = find_leaf(key);
    if (!leaf) return false;
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end() || *it != key) return false;
    std::size_t pos = static_cast<std::size_t>(it - leaf->keys.begin());
    leaf->keys.erase(it);
    leaf->values.erase(leaf->values.begin() + pos);
    return true;  // lazy: no merge/borrow; the tree stays a valid search structure
}
