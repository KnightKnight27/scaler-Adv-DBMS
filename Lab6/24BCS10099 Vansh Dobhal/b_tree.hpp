// b_tree.hpp
#pragma once

#include <vector>
#include <iostream>
#include <string>
#include <utility>
#include <stdexcept>
#include <algorithm>

namespace lab6 {

template <typename K, typename V, typename Compare = std::less<K>>
class BTree {
private:
    struct Node {
        std::vector<K> keys;
        std::vector<V> values;
        std::vector<Node*> children;
        bool is_leaf;

        Node(bool leaf) : is_leaf(leaf) {}
        ~Node() {
            for (Node* c : children) {
                delete c;
            }
        }
        
        bool is_full(int t) const {
            return keys.size() == static_cast<size_t>(2 * t - 1);
        }
        
        int num_keys() const {
            return static_cast<int>(keys.size());
        }
    };

    Node* root;
    int t;
    size_t item_count;
    Compare cmp;

    int find_index(const Node* node, const K& key) const {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key, cmp);
        return std::distance(node->keys.begin(), it);
    }

    void split_child(Node* parent, int index) {
        Node* full_node = parent->children[index];
        Node* new_node = new Node(full_node->is_leaf);
        
        // Move upper t-1 keys and values to new_node
        new_node->keys.assign(std::make_move_iterator(full_node->keys.begin() + t), std::make_move_iterator(full_node->keys.end()));
        new_node->values.assign(std::make_move_iterator(full_node->values.begin() + t), std::make_move_iterator(full_node->values.end()));
        
        if (!full_node->is_leaf) {
            new_node->children.assign(full_node->children.begin() + t, full_node->children.end());
            full_node->children.resize(t);
        }
        
        K mid_key = std::move(full_node->keys[t - 1]);
        V mid_val = std::move(full_node->values[t - 1]);
        
        full_node->keys.resize(t - 1);
        full_node->values.resize(t - 1);
        
        parent->children.insert(parent->children.begin() + index + 1, new_node);
        parent->keys.insert(parent->keys.begin() + index, std::move(mid_key));
        parent->values.insert(parent->values.begin() + index, std::move(mid_val));
    }

    bool insert_into_non_full(Node* node, const K& key, const V& value) {
        int idx = find_index(node, key);
        
        if (idx < node->num_keys() && !cmp(key, node->keys[idx]) && !cmp(node->keys[idx], key)) {
            node->values[idx] = value; // Update existing
            return false;
        }

        if (node->is_leaf) {
            node->keys.insert(node->keys.begin() + idx, key);
            node->values.insert(node->values.begin() + idx, value);
            item_count++;
            return true;
        }

        if (node->children[idx]->is_full(t)) {
            split_child(node, idx);
            if (cmp(node->keys[idx], key)) {
                idx++;
            } else if (!cmp(key, node->keys[idx]) && !cmp(node->keys[idx], key)) {
                node->values[idx] = value;
                return false;
            }
        }
        return insert_into_non_full(node->children[idx], key, value);
    }

    // Deletion Helpers
    bool remove_from_node(Node* node, const K& key) {
        int idx = find_index(node, key);
        bool found = (idx < node->num_keys() && !cmp(key, node->keys[idx]) && !cmp(node->keys[idx], key));

        if (node->is_leaf) {
            if (found) {
                node->keys.erase(node->keys.begin() + idx);
                node->values.erase(node->values.begin() + idx);
                return true;
            }
            return false;
        }

        if (found) {
            return remove_internal_node(node, idx);
        }

        bool is_last_child = (idx == node->num_keys());
        if (node->children[idx]->num_keys() < t) {
            fix_shortage(node, idx);
        }
        
        if (is_last_child && idx > node->num_keys()) {
            idx--;
        }
        return remove_from_node(node->children[idx], key);
    }

    bool remove_internal_node(Node* node, int idx) {
        Node* left_child = node->children[idx];
        Node* right_child = node->children[idx + 1];

        if (left_child->num_keys() >= t) {
            auto pred = extract_max(left_child);
            node->keys[idx] = std::move(pred.first);
            node->values[idx] = std::move(pred.second);
            return true;
        } else if (right_child->num_keys() >= t) {
            auto succ = extract_min(right_child);
            node->keys[idx] = std::move(succ.first);
            node->values[idx] = std::move(succ.second);
            return true;
        } else {
            merge_nodes(node, idx);
            K target = left_child->keys[t - 1]; // original key moved down
            return remove_from_node(left_child, target);
        }
    }

    std::pair<K, V> extract_max(Node* node) {
        if (node->is_leaf) {
            K k = std::move(node->keys.back());
            V v = std::move(node->values.back());
            node->keys.pop_back();
            node->values.pop_back();
            return {std::move(k), std::move(v)};
        }
        int last = node->num_keys();
        if (node->children[last]->num_keys() < t) {
            fix_shortage(node, last);
            last = node->num_keys(); // might have changed
        }
        return extract_max(node->children[last]);
    }

    std::pair<K, V> extract_min(Node* node) {
        if (node->is_leaf) {
            K k = std::move(node->keys.front());
            V v = std::move(node->values.front());
            node->keys.erase(node->keys.begin());
            node->values.erase(node->values.begin());
            return {std::move(k), std::move(v)};
        }
        if (node->children[0]->num_keys() < t) {
            fix_shortage(node, 0);
        }
        return extract_min(node->children[0]);
    }

    void fix_shortage(Node* parent, int idx) {
        Node* child = parent->children[idx];
        if (child->num_keys() >= t) return;

        Node* left_sib = (idx > 0) ? parent->children[idx - 1] : nullptr;
        Node* right_sib = (idx < parent->num_keys()) ? parent->children[idx + 1] : nullptr;

        if (left_sib && left_sib->num_keys() >= t) {
            child->keys.insert(child->keys.begin(), std::move(parent->keys[idx - 1]));
            child->values.insert(child->values.begin(), std::move(parent->values[idx - 1]));
            parent->keys[idx - 1] = std::move(left_sib->keys.back());
            parent->values[idx - 1] = std::move(left_sib->values.back());
            
            left_sib->keys.pop_back();
            left_sib->values.pop_back();
            
            if (!child->is_leaf) {
                child->children.insert(child->children.begin(), left_sib->children.back());
                left_sib->children.pop_back();
            }
        } else if (right_sib && right_sib->num_keys() >= t) {
            child->keys.push_back(std::move(parent->keys[idx]));
            child->values.push_back(std::move(parent->values[idx]));
            parent->keys[idx] = std::move(right_sib->keys.front());
            parent->values[idx] = std::move(right_sib->values.front());
            
            right_sib->keys.erase(right_sib->keys.begin());
            right_sib->values.erase(right_sib->values.begin());
            
            if (!child->is_leaf) {
                child->children.push_back(right_sib->children.front());
                right_sib->children.erase(right_sib->children.begin());
            }
        } else {
            if (right_sib) {
                merge_nodes(parent, idx);
            } else {
                merge_nodes(parent, idx - 1);
            }
        }
    }

    void merge_nodes(Node* parent, int idx) {
        Node* left = parent->children[idx];
        Node* right = parent->children[idx + 1];

        left->keys.push_back(std::move(parent->keys[idx]));
        left->values.push_back(std::move(parent->values[idx]));

        left->keys.insert(left->keys.end(), std::make_move_iterator(right->keys.begin()), std::make_move_iterator(right->keys.end()));
        left->values.insert(left->values.end(), std::make_move_iterator(right->values.begin()), std::make_move_iterator(right->values.end()));

        if (!left->is_leaf) {
            left->children.insert(left->children.end(), right->children.begin(), right->children.end());
            right->children.clear();
        }

        parent->keys.erase(parent->keys.begin() + idx);
        parent->values.erase(parent->values.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);

        delete right;
    }

    std::string check_invariants(const Node* node, bool is_root, int current_depth, int& expected_leaf_depth) const {
        int k = node->num_keys();
        if (k > 2 * t - 1) return "Too many keys in node";
        if (!is_root && k < t - 1) return "Too few keys in non-root node";
        if (is_root && k == 0 && !node->is_leaf) return "Root is empty but not leaf";

        for (int i = 1; i < k; ++i) {
            if (!cmp(node->keys[i - 1], node->keys[i])) return "Keys are not strictly sorted";
        }

        if (node->is_leaf) {
            if (expected_leaf_depth == -1) expected_leaf_depth = current_depth;
            else if (expected_leaf_depth != current_depth) return "Leaves are at different depths";
            if (!node->children.empty()) return "Leaf node has children";
            return "";
        }

        if (static_cast<int>(node->children.size()) != k + 1) return "Invalid number of children";

        for (int i = 0; i <= k; ++i) {
            const Node* c = node->children[i];
            for (const auto& key : c->keys) {
                if (i < k && !cmp(key, node->keys[i])) return "Child key >= right separator";
                if (i > 0 && !cmp(node->keys[i - 1], key)) return "Child key <= left separator";
            }
            std::string err = check_invariants(c, false, current_depth + 1, expected_leaf_depth);
            if (!err.empty()) return err;
        }

        return "";
    }

    template <typename F>
    void in_order_traverse(const Node* node, F& callback) const {
        if (!node) return;
        for (int i = 0; i < node->num_keys(); ++i) {
            if (!node->is_leaf) in_order_traverse(node->children[i], callback);
            callback(node->keys[i], node->values[i]);
        }
        if (!node->is_leaf) in_order_traverse(node->children.back(), callback);
    }

    void print_structure(const Node* node, int indent, std::ostream& os) const {
        os << std::string(indent * 2, ' ') << "[";
        for (int i = 0; i < node->num_keys(); ++i) {
            if (i > 0) os << ", ";
            os << node->keys[i];
        }
        os << "]" << (node->is_leaf ? " (leaf)\n" : "\n");
        for (Node* c : node->children) {
            print_structure(c, indent + 1, os);
        }
    }

    std::pair<Node*, int> search_node(const K& key) const {
        Node* curr = root;
        while (curr) {
            int idx = find_index(curr, key);
            if (idx < curr->num_keys() && !cmp(key, curr->keys[idx]) && !cmp(curr->keys[idx], key)) {
                return {curr, idx};
            }
            if (curr->is_leaf) return {nullptr, -1};
            curr = curr->children[idx];
        }
        return {nullptr, -1};
    }

public:
    explicit BTree(int degree = 3) : root(nullptr), t(std::max(2, degree)), item_count(0) {}

    ~BTree() {
        delete root;
    }

    BTree(const BTree&) = delete;
    BTree& operator=(const BTree&) = delete;

    bool insert(const K& key, const V& value) {
        if (!root) {
            root = new Node(true);
            root->keys.push_back(key);
            root->values.push_back(value);
            item_count++;
            return true;
        }

        if (root->is_full(t)) {
            Node* new_root = new Node(false);
            new_root->children.push_back(root);
            split_child(new_root, 0);
            root = new_root;
        }

        return insert_into_non_full(root, key, value);
    }

    bool remove(const K& key) {
        if (!root) return false;
        bool deleted = remove_from_node(root, key);
        
        if (root->keys.empty()) {
            Node* old_root = root;
            if (root->is_leaf) {
                root = nullptr;
            } else {
                root = root->children[0];
                old_root->children.clear(); // Avoid cascading deletes
            }
            delete old_root;
        }
        
        if (deleted) item_count--;
        return deleted;
    }

    bool contains(const K& key) const {
        return search_node(key).first != nullptr;
    }

    V& get(const K& key) {
        auto res = search_node(key);
        if (!res.first) throw std::out_of_range("Key not found in BTree");
        return res.first->values[res.second];
    }

    const V& get(const K& key) const {
        auto res = search_node(key);
        if (!res.first) throw std::out_of_range("Key not found in BTree");
        return res.first->values[res.second];
    }

    size_t size() const { return item_count; }
    bool is_empty() const { return item_count == 0; }
    int get_degree() const { return t; }

    template <typename F>
    void iterate(F&& callback) const {
        in_order_traverse(root, callback);
    }

    void display(std::ostream& os = std::cout) const {
        if (!root) {
            os << "Tree is empty.\n";
            return;
        }
        print_structure(root, 0, os);
    }

    std::string validate() const {
        if (!root) return "";
        int leaf_depth = -1;
        return check_invariants(root, true, 0, leaf_depth);
    }
};

} // namespace lab6
