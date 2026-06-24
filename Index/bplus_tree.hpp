#ifndef BPLUS_TREE_HPP
#define BPLUS_TREE_HPP

#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include <cassert>

template <typename Key, typename Value>
class BPlusTree {
public:
    explicit BPlusTree(size_t min_degree = 2)
        : min_degree_(min_degree),
          max_keys_(2 * min_degree - 1),
          min_keys_(min_degree - 1) {
        root_ = new LeafNode();
    }

    ~BPlusTree() {
        destroy(root_);
    }

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    std::optional<Value> search(Key key, size_t* nodes_visited = nullptr) const {
        Node* leaf = find_leaf(key, nodes_visited);
        if (!leaf) return std::nullopt;

        auto* leaf_node = static_cast<LeafNode*>(leaf);
        for (size_t i = 0; i < leaf_node->keys.size(); ++i) {
            if (leaf_node->keys[i] == key) {
                return leaf_node->values[i];
            }
        }
        return std::nullopt;
    }

    void insert(Key key, const Value& value) {
        if (search(key).has_value()) {
            std::cout << "Key " << key << " already exists. Skipping duplicate insert.\n";
            return;
        }

        if (root_->is_leaf && static_cast<LeafNode*>(root_)->keys.size() == max_keys_) {
            Node* old_root = root_;
            InternalNode* new_root = new InternalNode();
            new_root->children.push_back(old_root);
            split_child(new_root, 0);
            root_ = new_root;
            insert_non_full(root_, key, value);
        } else {
            insert_non_full(root_, key, value);
        }
    }

    std::vector<std::pair<Key, Value>> range_search(Key low, Key high) const {
        std::vector<std::pair<Key, Value>> result;
        if (low > high) return result;

        Node* leaf = find_leaf(low);
        while (leaf) {
            auto* leaf_node = static_cast<LeafNode*>(leaf);
            for (size_t i = 0; i < leaf_node->keys.size(); ++i) {
                if (leaf_node->keys[i] >= low && leaf_node->keys[i] <= high) {
                    result.emplace_back(leaf_node->keys[i], leaf_node->values[i]);
                }
            }
            if (!leaf_node->keys.empty() && leaf_node->keys.back() > high) {
                break;
            }
            leaf = leaf_node->next;
        }
        return result;
    }

    void print() const {
        if (!root_) {
            std::cout << "[Empty B+ Tree]\n";
            return;
        }
        print_node(root_, 0);
    }

    bool validate() const {
        if (!root_) return true;
        int expected_leaf_depth = -1;
        LeafNode* leftmost = nullptr;
        return validate_node(root_, nullptr, nullptr, 0, &expected_leaf_depth, &leftmost);
    }

    size_t size() const {
        return count_keys(root_);
    }

private:
    struct Node {
        bool is_leaf = true;
        virtual ~Node() = default;
    protected:
        explicit Node(bool leaf) : is_leaf(leaf) {}
    };

    struct LeafNode : Node {
        std::vector<Key> keys;
        std::vector<Value> values;
        LeafNode* next = nullptr;
        LeafNode() : Node(true) {}
    };

    struct InternalNode : Node {
        std::vector<Key> keys;
        std::vector<Node*> children;
        InternalNode() : Node(false) {}
    };

    size_t min_degree_;
    size_t max_keys_;
    size_t min_keys_;
    Node* root_;

    Node* find_leaf(Key key, size_t* nodes_visited = nullptr) const {
        Node* current = root_;
        size_t visited = 0;
        while (current && !current->is_leaf) {
            ++visited;
            auto* internal = static_cast<InternalNode*>(current);
            size_t i = 0;
            while (i < internal->keys.size() && key >= internal->keys[i]) {
                ++i;
            }
            current = internal->children[i];
        }
        if (current) {
            ++visited;
        }
        if (nodes_visited) {
            *nodes_visited = visited;
        }
        return current;
    }

    void insert_non_full(Node* node, Key key, const Value& value) {
        if (node->is_leaf) {
            auto* leaf = static_cast<LeafNode*>(node);
            size_t pos = 0;
            while (pos < leaf->keys.size() && leaf->keys[pos] < key) {
                ++pos;
            }
            leaf->keys.insert(leaf->keys.begin() + pos, key);
            leaf->values.insert(leaf->values.begin() + pos, value);
            return;
        }

        auto* internal = static_cast<InternalNode*>(node);
        size_t i = internal->keys.size();
        while (i > 0 && key < internal->keys[i - 1]) {
            --i;
        }

        if (internal->children[i]->is_leaf) {
            auto* child_leaf = static_cast<LeafNode*>(internal->children[i]);
            if (child_leaf->keys.size() == max_keys_) {
                split_child(internal, i);
                if (key > internal->keys[i]) {
                    ++i;
                }
            }
        } else {
            auto* child_internal = static_cast<InternalNode*>(internal->children[i]);
            if (child_internal->keys.size() == max_keys_) {
                split_child(internal, i);
                if (key > internal->keys[i]) {
                    ++i;
                }
            }
        }

        insert_non_full(internal->children[i], key, value);
    }

    void split_child(InternalNode* parent, size_t child_index) {
        Node* full_child = parent->children[child_index];

        if (full_child->is_leaf) {
            auto* leaf = static_cast<LeafNode*>(full_child);
            auto* new_leaf = new LeafNode();

            size_t mid = min_degree_;
            new_leaf->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
            new_leaf->values.assign(leaf->values.begin() + mid, leaf->values.end());
            leaf->keys.resize(mid);
            leaf->values.resize(mid);

            new_leaf->next = leaf->next;
            leaf->next = new_leaf;

            Key promoted_key = new_leaf->keys.front();
            parent->keys.insert(parent->keys.begin() + child_index, promoted_key);
            parent->children.insert(parent->children.begin() + child_index + 1, new_leaf);
        } else {
            auto* internal = static_cast<InternalNode*>(full_child);
            auto* new_internal = new InternalNode();

            size_t mid_key_index = min_degree_ - 1;
            Key promoted_key = internal->keys[mid_key_index];

            new_internal->keys.assign(internal->keys.begin() + mid_key_index + 1, internal->keys.end());
            new_internal->children.assign(internal->children.begin() + min_degree_, internal->children.end());

            internal->keys.resize(mid_key_index);
            internal->children.resize(min_degree_);

            parent->keys.insert(parent->keys.begin() + child_index, promoted_key);
            parent->children.insert(parent->children.begin() + child_index + 1, new_internal);
        }
    }

    size_t count_keys(Node* node) const {
        if (!node) return 0;
        if (node->is_leaf) {
            return static_cast<LeafNode*>(node)->keys.size();
        }
        size_t total = 0;
        auto* internal = static_cast<InternalNode*>(node);
        for (Node* child : internal->children) {
            total += count_keys(child);
        }
        return total;
    }

    void print_node(Node* node, int level) const {
        std::string indent(level * 2, ' ');
        if (node->is_leaf) {
            auto* leaf = static_cast<LeafNode*>(node);
            std::cout << indent << "LEAF: ";
            for (size_t i = 0; i < leaf->keys.size(); ++i) {
                std::cout << "(" << leaf->keys[i] << "," << leaf->values[i] << ") ";
            }
            std::cout << "\n";
            return;
        }

        auto* internal = static_cast<InternalNode*>(node);
        std::cout << indent << "INTERNAL keys: ";
        for (const Key& k : internal->keys) {
            std::cout << k << " ";
        }
        std::cout << "\n";
        for (Node* child : internal->children) {
            print_node(child, level + 1);
        }
    }

    bool validate_node(Node* node, const Key* min_bound, const Key* max_bound,
                       int depth, int* expected_leaf_depth, LeafNode** leftmost_leaf) const {
        if (!node) return true;

        if (node->is_leaf) {
            auto* leaf = static_cast<LeafNode*>(node);
            if (leaf->keys.size() != leaf->values.size()) return false;

            if (*expected_leaf_depth < 0) {
                *expected_leaf_depth = depth;
            } else if (*expected_leaf_depth != depth) {
                return false;
            }

            for (size_t i = 1; i < leaf->keys.size(); ++i) {
                if (leaf->keys[i - 1] >= leaf->keys[i]) return false;
            }
            for (const Key& k : leaf->keys) {
                if (min_bound && k < *min_bound) return false;
                if (max_bound && k > *max_bound) return false;
            }

            if (*leftmost_leaf == nullptr) {
                *leftmost_leaf = leaf;
            }
            return true;
        }

        auto* internal = static_cast<InternalNode*>(node);
        if (internal->keys.size() + 1 != internal->children.size()) return false;

        for (size_t i = 1; i < internal->keys.size(); ++i) {
            if (internal->keys[i - 1] >= internal->keys[i]) return false;
        }

        for (size_t i = 0; i < internal->children.size(); ++i) {
            const Key* child_min = (i == 0) ? min_bound : &internal->keys[i - 1];
            const Key* child_max = (i == internal->keys.size()) ? max_bound : &internal->keys[i];
            if (!validate_node(internal->children[i], child_min, child_max,
                               depth + 1, expected_leaf_depth, leftmost_leaf)) {
                return false;
            }
        }
        return true;
    }

    void destroy(Node* node) {
        if (!node) return;
        if (!node->is_leaf) {
            auto* internal = static_cast<InternalNode*>(node);
            for (Node* child : internal->children) {
                destroy(child);
            }
        }
        delete node;
    }
};

// Course-style wrapper matching the assignment skeleton API.
template <typename Key, typename Row>
class DB {
public:
    struct Entry {
        Key key;
        Row row;
        bool found = false;
        size_t nodes_visited = 0;
    };

    explicit DB(size_t degree = 2) : tree_(degree) {}

    Entry search(Key key) {
        Entry result{};
        result.key = key;
        size_t visited = 0;
        auto value = tree_.search(key, &visited);
        result.nodes_visited = visited;
        if (value.has_value()) {
            result.row = value.value();
            result.found = true;
        }
        return result;
    }

    void insert(Key key, const Row& row) {
        tree_.insert(key, row);
    }

    std::vector<Entry> range_search(Key low, Key high) {
        std::vector<Entry> entries;
        for (const auto& [k, v] : tree_.range_search(low, high)) {
            entries.push_back({k, v, true});
        }
        return entries;
    }

    void print() const { tree_.print(); }
    bool validate() const { return tree_.validate(); }
    size_t size() const { return tree_.size(); }

private:
    BPlusTree<Key, Row> tree_;
};

#endif
