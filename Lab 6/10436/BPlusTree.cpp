#include "BPlusTree.h"
#include <algorithm>
#include <queue>
#include <cassert>

// ---- Node constructor ----

Node::Node(bool leaf)
    : isLeaf(leaf), next(nullptr), parent(nullptr) {}

// ---- BPlusTree constructor / destructor ----

BPlusTree::BPlusTree() : root(nullptr) {}

BPlusTree::~BPlusTree() {
    destroy_tree(root);
}

void BPlusTree::destroy_tree(Node* node) {
    if (!node) return;
    if (!node->isLeaf)
        for (Node* child : node->children)
            destroy_tree(child);
    delete node;
}

// ---- find_leaf ----

Node* BPlusTree::find_leaf(int key) const {
    if (!root) return nullptr;
    Node* cur = root;
    while (!cur->isLeaf) {
        int i = 0;
        while (i < (int)cur->keys.size() && key >= cur->keys[i])
            ++i;
        cur = cur->children[i];
    }
    return cur;
}

// ---- insert_into_leaf ----

void BPlusTree::insert_into_leaf(Node* leaf, int key, const std::string& value) {
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    int idx = (int)(it - leaf->keys.begin());
    leaf->keys.insert(it, key);
    leaf->values.insert(leaf->values.begin() + idx, value);
}

// ---- split_leaf ----

void BPlusTree::split_leaf(Node* leaf) {
    int total = (int)leaf->keys.size();          // MAX_KEYS + 1
    int split  = (total + 1) / 2;               // ceil((MAX_KEYS+1)/2)

    Node* new_leaf = new Node(true);
    new_leaf->parent = leaf->parent;

    new_leaf->keys.assign(leaf->keys.begin() + split, leaf->keys.end());
    new_leaf->values.assign(leaf->values.begin() + split, leaf->values.end());

    leaf->keys.resize(split);
    leaf->values.resize(split);

    new_leaf->next = leaf->next;
    leaf->next     = new_leaf;

    insert_into_parent(leaf, new_leaf->keys[0], new_leaf);
}

// ---- split_internal ----

void BPlusTree::split_internal(Node* node) {
    int total  = (int)node->keys.size();          // MAX_KEYS + 1
    int mid    = total / 2;
    int push_key = node->keys[mid];

    Node* new_node = new Node(false);
    new_node->parent = node->parent;

    new_node->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    new_node->children.assign(node->children.begin() + mid + 1, node->children.end());

    node->keys.resize(mid);
    node->children.resize(mid + 1);

    for (Node* child : new_node->children)
        child->parent = new_node;

    insert_into_parent(node, push_key, new_node);
}

// ---- insert_into_parent ----

void BPlusTree::insert_into_parent(Node* left, int push_up_key, Node* right) {
    if (!left->parent) {
        // left was the root — create a new root
        Node* new_root = new Node(false);
        new_root->keys.push_back(push_up_key);
        new_root->children.push_back(left);
        new_root->children.push_back(right);
        left->parent  = new_root;
        right->parent = new_root;
        root          = new_root;
        return;
    }

    Node* parent = left->parent;
    right->parent = parent;

    // Find insertion position: after left in parent->children
    auto it = std::find(parent->children.begin(), parent->children.end(), left);
    int pos = (int)(it - parent->children.begin());

    parent->keys.insert(parent->keys.begin() + pos, push_up_key);
    parent->children.insert(parent->children.begin() + pos + 1, right);

    if ((int)parent->keys.size() > MAX_KEYS)
        split_internal(parent);
}

// ---- insert (public) ----

void BPlusTree::insert(int key, const std::string& value) {
    if (!root) {
        root = new Node(true);
        root->keys.push_back(key);
        root->values.push_back(value);
        return;
    }

    Node* leaf = find_leaf(key);
    insert_into_leaf(leaf, key, value);

    if ((int)leaf->keys.size() > MAX_KEYS)
        split_leaf(leaf);
}

// ---- search ----

std::optional<std::string> BPlusTree::search(int key) const {
    Node* leaf = find_leaf(key);
    if (!leaf) return std::nullopt;
    for (int i = 0; i < (int)leaf->keys.size(); ++i)
        if (leaf->keys[i] == key)
            return leaf->values[i];
    return std::nullopt;
}

// ---- range_scan ----

std::vector<std::pair<int, std::string>> BPlusTree::range_scan(int lo, int hi) const {
    std::vector<std::pair<int, std::string>> result;
    Node* leaf = find_leaf(lo);
    while (leaf) {
        for (int i = 0; i < (int)leaf->keys.size(); ++i) {
            if (leaf->keys[i] > hi) return result;
            if (leaf->keys[i] >= lo)
                result.emplace_back(leaf->keys[i], leaf->values[i]);
        }
        leaf = leaf->next;
    }
    return result;
}

// ---- delete_from_leaf ----

void BPlusTree::delete_from_leaf(Node* leaf, int key) {
    auto it = std::find(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end()) return;
    int idx = (int)(it - leaf->keys.begin());
    leaf->keys.erase(it);
    leaf->values.erase(leaf->values.begin() + idx);
}

// ---- borrow_from_left_sibling ----

bool BPlusTree::borrow_from_left_sibling(Node* node, Node* sibling, Node* parent, int separator_idx) {
    int min_keys = (MAX_KEYS + 1) / 2;  // ceil(MAX_KEYS/2) = 2
    if ((int)sibling->keys.size() <= min_keys) return false;

    if (node->isLeaf) {
        // Move last key/value from sibling to front of node
        node->keys.insert(node->keys.begin(), sibling->keys.back());
        node->values.insert(node->values.begin(), sibling->values.back());
        sibling->keys.pop_back();
        sibling->values.pop_back();
        parent->keys[separator_idx] = node->keys[0];
    } else {
        // Rotate through parent separator
        node->keys.insert(node->keys.begin(), parent->keys[separator_idx]);
        node->children.insert(node->children.begin(), sibling->children.back());
        node->children.front()->parent = node;
        parent->keys[separator_idx] = sibling->keys.back();
        sibling->keys.pop_back();
        sibling->children.pop_back();
    }
    return true;
}

// ---- borrow_from_right_sibling ----

bool BPlusTree::borrow_from_right_sibling(Node* node, Node* sibling, Node* parent, int separator_idx) {
    int min_keys = (MAX_KEYS + 1) / 2;
    if ((int)sibling->keys.size() <= min_keys) return false;

    if (node->isLeaf) {
        node->keys.push_back(sibling->keys.front());
        node->values.push_back(sibling->values.front());
        sibling->keys.erase(sibling->keys.begin());
        sibling->values.erase(sibling->values.begin());
        parent->keys[separator_idx] = sibling->keys[0];
    } else {
        node->keys.push_back(parent->keys[separator_idx]);
        node->children.push_back(sibling->children.front());
        node->children.back()->parent = node;
        parent->keys[separator_idx] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());
        sibling->children.erase(sibling->children.begin());
    }
    return true;
}

// ---- merge_nodes ----

void BPlusTree::merge_nodes(Node* left, Node* right, Node* parent, int separator_idx) {
    if (left->isLeaf) {
        // Append right's keys/values to left
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        left->values.insert(left->values.end(), right->values.begin(), right->values.end());
        left->next = right->next;
    } else {
        // Pull down separator from parent, then append right's keys/children
        left->keys.push_back(parent->keys[separator_idx]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        for (Node* child : right->children) {
            child->parent = left;
            left->children.push_back(child);
        }
    }

    // Remove separator and right pointer from parent
    parent->keys.erase(parent->keys.begin() + separator_idx);
    parent->children.erase(parent->children.begin() + separator_idx + 1);

    delete right;

    if (parent == root && parent->keys.empty()) {
        root = left;
        left->parent = nullptr;
        delete parent;
    } else if (parent != root) {
        int min_keys = (MAX_KEYS + 1) / 2;
        if ((int)parent->keys.size() < min_keys)
            fix_underflow(parent);
    }
}

// ---- fix_underflow ----

void BPlusTree::fix_underflow(Node* node) {
    if (node == root) return;

    Node* parent = node->parent;
    auto it = std::find(parent->children.begin(), parent->children.end(), node);
    int pos = (int)(it - parent->children.begin());

    // Try left sibling
    if (pos > 0) {
        Node* left_sib = parent->children[pos - 1];
        if (borrow_from_left_sibling(node, left_sib, parent, pos - 1))
            return;
    }

    // Try right sibling
    if (pos < (int)parent->children.size() - 1) {
        Node* right_sib = parent->children[pos + 1];
        if (borrow_from_right_sibling(node, right_sib, parent, pos))
            return;
    }

    // Merge
    if (pos > 0) {
        // Merge node into left sibling
        merge_nodes(parent->children[pos - 1], node, parent, pos - 1);
    } else {
        // Merge right sibling into node
        merge_nodes(node, parent->children[pos + 1], parent, pos);
    }
}

// ---- delete_key (public) ----

void BPlusTree::delete_key(int key) {
    Node* leaf = find_leaf(key);
    if (!leaf) return;

    auto it = std::find(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end()) return;

    delete_from_leaf(leaf, key);

    if (leaf == root) return;  // single-node tree, no underflow concern

    int min_keys = (MAX_KEYS + 1) / 2;
    if ((int)leaf->keys.size() < min_keys)
        fix_underflow(leaf);
}

// ---- print_tree ----

void BPlusTree::print_tree() const {
    if (!root) { std::cout << "(empty tree)\n"; return; }

    std::queue<std::pair<Node*, int>> q;
    q.push({root, 0});
    int cur_depth = 0;
    bool first_in_level = true;

    while (!q.empty()) {
        auto [node, depth] = q.front();
        q.pop();

        if (depth != cur_depth) {
            std::cout << "\n";
            cur_depth    = depth;
            first_in_level = true;
        }

        if (!first_in_level) std::cout << "  ";
        first_in_level = false;

        std::cout << "[";
        for (int i = 0; i < (int)node->keys.size(); ++i) {
            if (i > 0) std::cout << " | ";
            std::cout << node->keys[i];
        }
        std::cout << "]";

        if (!node->isLeaf)
            for (Node* child : node->children)
                q.push({child, depth + 1});
    }
    std::cout << "\n";
}
