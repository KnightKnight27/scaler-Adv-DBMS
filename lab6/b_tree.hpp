#pragma once

#include <cstddef>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace adbms {

template <typename Key, typename Value, typename Compare = std::less<Key>>
class b_tree {
public:
    explicit b_tree(int t = 3) : t_(t < 2 ? 2 : t) {}

    b_tree(const b_tree&) = delete;
    b_tree& operator=(const b_tree&) = delete;
    b_tree(b_tree&&) = delete;
    b_tree& operator=(b_tree&&) = delete;

    ~b_tree() { delete root_; }

    bool insert(const Key& key, const Value& value) {
        if (!root_) {
            root_ = new Node(true);
            root_->keys.push_back(key);
            root_->values.push_back(value);
            ++size_;
            return true;
        }

        if (root_->is_full(t_)) {
            Node* fresh = new Node(false);
            fresh->children.push_back(root_);
            split_child(fresh, 0);
            root_ = fresh;
        }

        return insert_nonfull(root_, key, value);
    }

    bool erase(const Key& key) {
        if (!root_) return false;

        bool removed = erase_from(root_, key);
        if (!removed) return false;

        if (root_->keys.empty()) {
            if (root_->leaf) {
                delete root_;
                root_ = nullptr;
            } else {
                Node* old = root_;
                root_ = old->children.front();
                old->children.clear();
                delete old;
            }
        }

        --size_;
        return true;
    }

    bool contains(const Key& key) const {
        return locate(key).first != nullptr;
    }

    Value& at(const Key& key) {
        auto [node, idx] = locate(key);
        if (!node) throw std::out_of_range("b_tree::at: key not found");
        return node->values[idx];
    }

    const Value& at(const Key& key) const {
        auto [node, idx] = locate(key);
        if (!node) throw std::out_of_range("b_tree::at: key not found");
        return node->values[idx];
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    int degree() const { return t_; }

    template <typename F>
    void in_order(F&& visit) const {
        if (root_) in_order_rec(root_, visit);
    }

    void print(std::ostream& os = std::cout) const {
        if (!root_) {
            os << "<empty>\n";
            return;
        }
        print_rec(root_, 0, os);
    }

    std::string verify() const {
        if (!root_) return {};
        int leaf_depth = -1;
        return verify_rec(root_, true, 0, leaf_depth, nullptr, nullptr);
    }

private:
    struct Node {
        std::vector<Key> keys;
        std::vector<Value> values;
        std::vector<Node*> children;
        bool leaf;

        explicit Node(bool is_leaf) : leaf(is_leaf) {}
        ~Node() { for (Node* child : children) delete child; }

        bool is_full(int t) const { return static_cast<int>(keys.size()) == 2 * t - 1; }
        int key_count() const { return static_cast<int>(keys.size()); }
    };

    Node* root_ = nullptr;
    int t_;
    std::size_t size_ = 0;
    Compare cmp_{};

    bool equal_keys(const Key& a, const Key& b) const {
        return !cmp_(a, b) && !cmp_(b, a);
    }

    int lower_bound_in(const Node* node, const Key& key) const {
        int i = 0;
        while (i < node->key_count() && cmp_(node->keys[i], key)) ++i;
        return i;
    }

    std::pair<Node*, int> locate(const Key& key) {
        Node* cur = root_;
        while (cur) {
            int i = lower_bound_in(cur, key);
            if (i < cur->key_count() && equal_keys(cur->keys[i], key)) return {cur, i};
            if (cur->leaf) return {nullptr, 0};
            cur = cur->children[i];
        }
        return {nullptr, 0};
    }

    std::pair<const Node*, int> locate(const Key& key) const {
        const Node* cur = root_;
        while (cur) {
            int i = lower_bound_in(cur, key);
            if (i < cur->key_count() && equal_keys(cur->keys[i], key)) return {cur, i};
            if (cur->leaf) return {nullptr, 0};
            cur = cur->children[i];
        }
        return {nullptr, 0};
    }

    void split_child(Node* parent, int idx) {
        Node* left = parent->children[idx];
        Node* right = new Node(left->leaf);
        const int mid = t_ - 1;

        for (int i = t_; i < static_cast<int>(left->keys.size()); ++i) {
            right->keys.push_back(std::move(left->keys[i]));
            right->values.push_back(std::move(left->values[i]));
        }

        if (!left->leaf) {
            for (int i = t_; i < static_cast<int>(left->children.size()); ++i) {
                right->children.push_back(left->children[i]);
            }
            left->children.resize(t_);
        }

        Key median_key = std::move(left->keys[mid]);
        Value median_value = std::move(left->values[mid]);

        left->keys.resize(mid);
        left->values.resize(mid);

        parent->children.insert(parent->children.begin() + idx + 1, right);
        parent->keys.insert(parent->keys.begin() + idx, std::move(median_key));
        parent->values.insert(parent->values.begin() + idx, std::move(median_value));
    }

    bool insert_nonfull(Node* node, const Key& key, const Value& value) {
        int idx = lower_bound_in(node, key);

        if (node->leaf) {
            if (idx < node->key_count() && equal_keys(node->keys[idx], key)) {
                node->values[idx] = value;
                return false;
            }
            node->keys.insert(node->keys.begin() + idx, key);
            node->values.insert(node->values.begin() + idx, value);
            ++size_;
            return true;
        }

        if (idx < node->key_count() && equal_keys(node->keys[idx], key)) {
            node->values[idx] = value;
            return false;
        }

        if (node->children[idx]->is_full(t_)) {
            split_child(node, idx);
            if (equal_keys(node->keys[idx], key)) {
                node->values[idx] = value;
                return false;
            }
            if (cmp_(node->keys[idx], key)) ++idx;
        }

        return insert_nonfull(node->children[idx], key, value);
    }

    bool erase_from(Node* node, const Key& key) {
        int idx = lower_bound_in(node, key);
        bool found_here = (idx < node->key_count() && equal_keys(node->keys[idx], key));

        if (found_here && node->leaf) {
            node->keys.erase(node->keys.begin() + idx);
            node->values.erase(node->values.begin() + idx);
            return true;
        }

        if (found_here) {
            return erase_internal(node, idx, key);
        }

        if (node->leaf) return false;

        idx = fix_child_before_descent(node, idx);
        return erase_from(node->children[idx], key);
    }

    bool erase_internal(Node* node, int idx, const Key& key) {
        Node* left = node->children[idx];
        Node* right = node->children[idx + 1];

        if (left->key_count() >= t_) {
            auto [pred_k, pred_v] = remove_max(left);
            node->keys[idx] = std::move(pred_k);
            node->values[idx] = std::move(pred_v);
            return true;
        }

        if (right->key_count() >= t_) {
            auto [succ_k, succ_v] = remove_min(right);
            node->keys[idx] = std::move(succ_k);
            node->values[idx] = std::move(succ_v);
            return true;
        }

        merge_children(node, idx);
        return erase_from(left, key);
    }

    std::pair<Key, Value> remove_min(Node* node) {
        if (node->leaf) {
            Key k = std::move(node->keys.front());
            Value v = std::move(node->values.front());
            node->keys.erase(node->keys.begin());
            node->values.erase(node->values.begin());
            return {std::move(k), std::move(v)};
        }

        int idx = fix_child_before_descent(node, 0);
        return remove_min(node->children[idx]);
    }

    std::pair<Key, Value> remove_max(Node* node) {
        if (node->leaf) {
            Key k = std::move(node->keys.back());
            Value v = std::move(node->values.back());
            node->keys.pop_back();
            node->values.pop_back();
            return {std::move(k), std::move(v)};
        }

        int idx = fix_child_before_descent(node, node->key_count());
        return remove_max(node->children[idx]);
    }

    int fix_child_before_descent(Node* parent, int idx) {
        Node* child = parent->children[idx];
        if (child->key_count() >= t_) return idx;

        Node* left = (idx > 0) ? parent->children[idx - 1] : nullptr;
        Node* right = (idx < parent->key_count()) ? parent->children[idx + 1] : nullptr;

        if (left && left->key_count() >= t_) {
            child->keys.insert(child->keys.begin(), std::move(parent->keys[idx - 1]));
            child->values.insert(child->values.begin(), std::move(parent->values[idx - 1]));

            parent->keys[idx - 1] = std::move(left->keys.back());
            parent->values[idx - 1] = std::move(left->values.back());

            left->keys.pop_back();
            left->values.pop_back();

            if (!child->leaf) {
                child->children.insert(child->children.begin(), left->children.back());
                left->children.pop_back();
            }
            return idx;
        }

        if (right && right->key_count() >= t_) {
            child->keys.push_back(std::move(parent->keys[idx]));
            child->values.push_back(std::move(parent->values[idx]));

            parent->keys[idx] = std::move(right->keys.front());
            parent->values[idx] = std::move(right->values.front());

            right->keys.erase(right->keys.begin());
            right->values.erase(right->values.begin());

            if (!child->leaf) {
                child->children.push_back(right->children.front());
                right->children.erase(right->children.begin());
            }
            return idx;
        }

        if (right) {
            merge_children(parent, idx);
            return idx;
        }

        merge_children(parent, idx - 1);
        return idx - 1;
    }

    void merge_children(Node* parent, int idx) {
        Node* left = parent->children[idx];
        Node* right = parent->children[idx + 1];

        left->keys.push_back(std::move(parent->keys[idx]));
        left->values.push_back(std::move(parent->values[idx]));

        for (auto& k : right->keys) left->keys.push_back(std::move(k));
        for (auto& v : right->values) left->values.push_back(std::move(v));

        if (!left->leaf) {
            for (Node* child : right->children) left->children.push_back(child);
            right->children.clear();
        }

        parent->keys.erase(parent->keys.begin() + idx);
        parent->values.erase(parent->values.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);

        delete right;
    }

    template <typename F>
    void in_order_rec(const Node* node, F& visit) const {
        int n = node->key_count();
        for (int i = 0; i < n; ++i) {
            if (!node->leaf) in_order_rec(node->children[i], visit);
            visit(node->keys[i], node->values[i]);
        }
        if (!node->leaf) in_order_rec(node->children[n], visit);
    }

    static void print_rec(const Node* node, int depth, std::ostream& os) {
        os << std::string(static_cast<std::size_t>(depth) * 2, ' ') << "[";
        for (int i = 0; i < node->key_count(); ++i) {
            if (i) os << ' ';
            os << node->keys[i];
        }
        os << "]" << (node->leaf ? " (leaf)\n" : "\n");
        for (const Node* child : node->children) print_rec(child, depth + 1, os);
    }

    std::string verify_rec(const Node* node,
                           bool is_root,
                           int depth,
                           int& leaf_depth,
                           const Key* low,
                           const Key* high) const {
        int n = node->key_count();

        if (n > 2 * t_ - 1) return "node has more than 2t-1 keys";
        if (!is_root && n < t_ - 1) return "non-root has fewer than t-1 keys";
        if (is_root && !node->leaf && n < 1) return "internal root has no keys";

        for (int i = 1; i < n; ++i) {
            if (!cmp_(node->keys[i - 1], node->keys[i])) {
                return "keys inside a node are not strictly increasing";
            }
        }

        auto in_range = [&](const Key& x) {
            if (low && !cmp_(*low, x)) return false;
            if (high && !cmp_(x, *high)) return false;
            return true;
        };

        for (const auto& k : node->keys) {
            if (!in_range(k)) return "node key violates ancestor bounds";
        }

        if (node->leaf) {
            if (leaf_depth == -1) leaf_depth = depth;
            else if (leaf_depth != depth) return "leaves are at different depths";
            if (!node->children.empty()) return "leaf has children";
            return {};
        }

        if (static_cast<int>(node->children.size()) != n + 1) {
            return "internal node has incorrect child count";
        }

        for (int i = 0; i <= n; ++i) {
            const Key* child_low = (i == 0) ? low : &node->keys[i - 1];
            const Key* child_high = (i == n) ? high : &node->keys[i];
            std::string err = verify_rec(node->children[i], false, depth + 1, leaf_depth, child_low, child_high);
            if (!err.empty()) return err;
        }

        return {};
    }
};

} 