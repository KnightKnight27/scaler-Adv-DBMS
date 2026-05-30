// Header-only templated B-tree of minimum degree t, used as an ordered
// key->value map. Implements:
//   * insert    -- CLRS proactive-split top-down pass
//   * search / contains / at
//   * erase     -- CLRS chapter 18 deletion (three cases for hit-in-node,
//                  three cases for descend-with-fix)
//   * in_order  -- sorted (key, value) traversal
//   * verify    -- runtime invariant checker for all five B-tree properties
//
// Each node stores 2t-1 (key,value) pairs and up to 2t children. We keep
// keys and values in *parallel* vectors so cache behaviour is the same
// as the reference int-only tree; the value side never participates in
// the comparison decisions.

#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace adbms {

template <typename Key, typename Value, typename Compare = std::less<Key>>
class b_tree {
private:
    struct Node {
        std::vector<Key>   keys;
        std::vector<Value> values;
        std::vector<Node*> children;
        bool               is_leaf;

        explicit Node(bool leaf_status) 
            : is_leaf(leaf_status) {}
            
        ~Node() { 
            for (Node* child : children) {
                delete child; 
            }
        }

        [[nodiscard]] bool is_full(int t) const { 
            return static_cast<int>(keys.size()) == (2 * t - 1); 
        }
        
        [[nodiscard]] int key_count() const { 
            return static_cast<int>(keys.size()); 
        }
    };

    Node* m_root = nullptr;
    int         m_min_degree;
    std::size_t m_size = 0;
    Compare     m_comparator{};

public:
    explicit b_tree(int t = 3) 
        : m_min_degree(t < 2 ? 2 : t) {} // t < 2 is degenerate

    // Delete copy and move semantics
    b_tree(const b_tree&)            = delete;
    b_tree& operator=(const b_tree&) = delete;
    b_tree(b_tree&&)                 = delete;
    b_tree& operator=(b_tree&&)      = delete;

    ~b_tree() { 
        delete m_root; 
    }

    // --- Modifiers --------------------------------------------------------

    // Returns true on first insert, false on overwrite (in which case the
    // existing value is replaced).
    bool insert(const Key& key, const Value& value) {
        if (!m_root) {
            m_root = new Node(true);
            m_root->keys.push_back(key);
            m_root->values.push_back(value);
            ++m_size;
            return true;
        }

        if (m_root->is_full(m_min_degree)) {
            // Grow the tree's height by 1 -- only way the height ever increases.
            Node* fresh_node = new Node(false);
            fresh_node->children.push_back(m_root);
            split_child(fresh_node, 0);
            m_root = fresh_node;
        }

        return insert_nonfull(m_root, key, value);
    }

    bool erase(const Key& key) {
        if (!m_root) return false;

        bool removed = erase_from(m_root, key);
        
        // If the root collapsed to a single child (this can happen when
        // its only key was merged into a child), drop a level.
        if (m_root->keys.empty() && !m_root->is_leaf) {
            Node* old_root = m_root;
            m_root         = m_root->children[0];
            old_root->children.clear(); // prevent destructor cascade
            delete old_root;
        } 
        else if (m_root->keys.empty() && m_root->is_leaf) {
            delete m_root;
            m_root = nullptr;
        }

        if (removed) {
            --m_size;
        }
        return removed;
    }

    // --- Queries ----------------------------------------------------------

    [[nodiscard]] bool contains(const Key& key) const { 
        return find_node(key).first != nullptr; 
    }

    [[nodiscard]] Value& at(const Key& key) {
        auto [node, index] = find_node(key);
        if (!node) {
            throw std::out_of_range("b_tree::at -- key not found");
        }
        return node->values[index];
    }

    [[nodiscard]] const Value& at(const Key& key) const {
        auto [node, index] = find_node(key);
        if (!node) {
            throw std::out_of_range("b_tree::at -- key not found");
        }
        return node->values[index];
    }

    [[nodiscard]] std::size_t size()   const { return m_size; }
    [[nodiscard]] bool        empty()  const { return m_size == 0; }
    [[nodiscard]] int         degree() const { return m_min_degree; }

    // Sorted (key, value) traversal -- calls visit(k, v) on every entry.
    template <typename F>
    void in_order(F&& visit) const {
        if (m_root) {
            in_order_rec(m_root, visit);
        }
    }

    void print(std::ostream& os = std::cout) const {
        if (!m_root) { 
            os << "<empty>\n"; 
            return; 
        }
        print_rec(m_root, 0, os);
    }

    // Returns "" when all invariants hold; otherwise the first violation.
    [[nodiscard]] std::string verify() const {
        if (!m_root) return "";
        int leaf_depth = -1;
        return verify_rec(m_root, /*is_root=*/true, /*depth=*/0, leaf_depth);
    }

private:
    // Returns the slot `i` such that keys[i-1] < key <= keys[i].
    [[nodiscard]] int lower_bound_in(const Node* node, const Key& key) const {
        int index = 0;
        const int key_limit = node->key_count();
        while (index < key_limit && m_comparator(node->keys[index], key)) {
            ++index;
        }
        return index;
    }

    // --- Search ----------------------------------------------------------

    [[nodiscard]] std::pair<Node*, int> find_node(const Key& key) {
        Node* current = m_root;
        while (current) {
            int index = lower_bound_in(current, key);
            if (index < current->key_count() && !m_comparator(key, current->keys[index])) {
                return {current, index};
            }
            if (current->is_leaf) {
                return {nullptr, 0};
            }
            current = current->children[index];
        }
        return {nullptr, 0};
    }

    [[nodiscard]] std::pair<const Node*, int> find_node(const Key& key) const {
        const Node* current = m_root;
        while (current) {
            int index = lower_bound_in(current, key);
            if (index < current->key_count() && !m_comparator(key, current->keys[index])) {
                return {current, index};
            }
            if (current->is_leaf) {
                return {nullptr, 0};
            }
            current = current->children[index];
        }
        return {nullptr, 0};
    }

    // --- Split + Insert --------------------------------------------------

    // Splits parent->children[index] which must be full (2t-1 keys).
    void split_child(Node* parent, int index) {
        Node* target = parent->children[index];
        Node* sibling = new Node(target->is_leaf);

        // Upper t-1 keys/values move to sibling.
        sibling->keys.assign(target->keys.begin() + m_min_degree, target->keys.end());
        sibling->values.assign(target->values.begin() + m_min_degree, target->values.end());
        
        if (!target->is_leaf) {
            sibling->children.assign(target->children.begin() + m_min_degree, target->children.end());
            target->children.erase(target->children.begin() + m_min_degree, target->children.end());
        }

        // Promote the median into the parent.
        Key   median_key = std::move(target->keys[m_min_degree - 1]);
        Value median_val = std::move(target->values[m_min_degree - 1]);
        
        target->keys.erase(target->keys.begin() + (m_min_degree - 1), target->keys.end());
        target->values.erase(target->values.begin() + (m_min_degree - 1), target->values.end());

        parent->children.insert(parent->children.begin() + index + 1, sibling);
        parent->keys.insert(parent->keys.begin() + index, std::move(median_key));
        parent->values.insert(parent->values.begin() + index, std::move(median_val));
    }

    // Insert (k, v) into a non-full node. Returns true if size grew.
    bool insert_nonfull(Node* node, const Key& key, const Value& value) {
        int index = node->key_count() - 1;

        if (node->is_leaf) {
            // Check for an overwrite hit first.
            int slot = lower_bound_in(node, key);
            if (slot < node->key_count() && !m_comparator(key, node->keys[slot])) {
                node->values[slot] = value;
                return false;
            }
            node->keys.insert(node->keys.begin() + slot, key);
            node->values.insert(node->values.begin() + slot, value);
            ++m_size;
            return true;
        }

        // Internal node -- find the correct child.
        while (index >= 0 && m_comparator(key, node->keys[index])) {
            --index;
        }
        
        // If keys[index] == key, overwrite here.
        if (index >= 0 && !m_comparator(node->keys[index], key)) { 
            node->values[index] = value; 
            return false; 
        }
        ++index;

        if (node->children[index]->is_full(m_min_degree)) {
            split_child(node, index);
            if (m_comparator(node->keys[index], key)) {
                ++index;
            } else if (!m_comparator(key, node->keys[index])) { 
                node->values[index] = value; 
                return false; 
            }
        }
        return insert_nonfull(node->children[index], key, value);
    }

    // --- Erase (CLRS chapter 18) -----------------------------------------

    // Remove `key` from the subtree rooted at `node`. Caller guarantees `node` has
    // >= t keys *unless* `node` is the root.
    bool erase_from(Node* node, const Key& key) {
        int index = lower_bound_in(node, key);
        bool found_here = (index < node->key_count()) && !m_comparator(key, node->keys[index]);

        if (found_here && node->is_leaf) {                           // Case 1: leaf hit
            node->keys.erase(node->keys.begin() + index);
            node->values.erase(node->values.begin() + index);
            return true;
        }

        if (found_here) {                                            // Case 2: internal hit
            return erase_internal(node, index);
        }

        if (node->is_leaf) return false;                             // Not in tree

        // Case 3: must descend; fix child first so it has >= t keys.
        bool is_last_child = (index == node->key_count());
        if (node->children[index]->key_count() < m_min_degree) {
            ensure_min_t(node, index);
        }

        // After fix, the child index might have shifted (merge collapses
        // children[i] and children[i+1] into children[i]).
        if (is_last_child && index > node->key_count()) {
            --index;
        }
        return erase_from(node->children[index], key);
    }

    // Internal hit: replace with predecessor or successor, or merge.
    bool erase_internal(Node* node, int index) {
        Node* left_child  = node->children[index];
        Node* right_child = node->children[index + 1];

        if (left_child->key_count() >= m_min_degree) {
            // Case 2a: take predecessor
            auto [pred_key, pred_val] = pop_max(left_child);
            node->keys[index]   = std::move(pred_key);
            node->values[index] = std::move(pred_val);
            return true;
        }

        if (right_child->key_count() >= m_min_degree) {
            // Case 2b: take successor
            auto [succ_key, succ_val] = pop_min(right_child);
            node->keys[index]   = std::move(succ_key);
            node->values[index] = std::move(succ_val);
            return true;
        }

        // Case 2c: both children minimal -- merge k down into them, then
        // recurse into the merged child to actually remove it.
        merge_children(node, index);
        Key target_key = left_child->keys[m_min_degree - 1];
        return erase_from(left_child, target_key);
    }

    // Pop the smallest entry in the subtree rooted at n. Caller guarantees
    // n has >= t keys at entry; we maintain that as we descend.
    std::pair<Key, Value> pop_min(Node* node) {
        if (node->is_leaf) {
            Key   key = std::move(node->keys.front());
            Value val = std::move(node->values.front());
            node->keys.erase(node->keys.begin());
            node->values.erase(node->values.begin());
            return {std::move(key), std::move(val)};
        }

        if (node->children[0]->key_count() < m_min_degree) {
            ensure_min_t(node, 0);
        }
        return pop_min(node->children[0]);
    }

    std::pair<Key, Value> pop_max(Node* node) {
        if (node->is_leaf) {
            Key   key = std::move(node->keys.back());
            Value val = std::move(node->values.back());
            node->keys.pop_back(); 
            node->values.pop_back();
            return {std::move(key), std::move(val)};
        }

        int last_idx = node->key_count();
        if (node->children[last_idx]->key_count() < m_min_degree) {
            ensure_min_t(node, last_idx);
        }
        return pop_max(node->children[node->key_count()]);
    }

    // Make sure children[index] has >= t keys by borrowing or merging.
    void ensure_min_t(Node* parent, int index) {
        Node* target = parent->children[index];
        if (target->key_count() >= m_min_degree) return;

        Node* left_sibling  = (index > 0) ? parent->children[index - 1] : nullptr;
        Node* right_sibling = (index < parent->key_count()) ? parent->children[index + 1] : nullptr;

        if (left_sibling && left_sibling->key_count() >= m_min_degree) {
            // 3a-left: rotate one key from left sibling through parent.
            target->keys.insert(target->keys.begin(), std::move(parent->keys[index - 1]));
            target->values.insert(target->values.begin(), std::move(parent->values[index - 1]));
            
            parent->keys[index - 1]   = std::move(left_sibling->keys.back());
            parent->values[index - 1] = std::move(left_sibling->values.back());
            
            left_sibling->keys.pop_back();
            left_sibling->values.pop_back();
            
            if (!target->is_leaf) {
                target->children.insert(target->children.begin(), left_sibling->children.back());
                left_sibling->children.pop_back();
            }
            return;
        }

        if (right_sibling && right_sibling->key_count() >= m_min_degree) {
            // 3a-right: rotate one key from right sibling through parent.
            target->keys.push_back(std::move(parent->keys[index]));
            target->values.push_back(std::move(parent->values[index]));
            
            parent->keys[index]   = std::move(right_sibling->keys.front());
            parent->values[index] = std::move(right_sibling->values.front());
            
            right_sibling->keys.erase(right_sibling->keys.begin());
            right_sibling->values.erase(right_sibling->values.begin());
            
            if (!target->is_leaf) {
                target->children.push_back(right_sibling->children.front());
                right_sibling->children.erase(right_sibling->children.begin());
            }
            return;
        }

        // 3b: merge with a sibling.
        if (right_sibling) {
            merge_children(parent, index);       // merges index and index+1 into index
        } else {
            merge_children(parent, index - 1);   // merges index-1 and index into index-1
        }
    }

    // Merge parent->children[index] and parent->children[index+1] into a single
    // node containing 2t-1 keys with parent->keys[index] dropped down between.
    void merge_children(Node* parent, int index) {
        Node* left_child  = parent->children[index];
        Node* right_child = parent->children[index + 1];

        left_child->keys.push_back(std::move(parent->keys[index]));
        left_child->values.push_back(std::move(parent->values[index]));
        
        for (auto& k : right_child->keys)   left_child->keys.push_back(std::move(k));
        for (auto& v : right_child->values) left_child->values.push_back(std::move(v));
        
        if (!left_child->is_leaf) {
            for (Node* c : right_child->children) {
                left_child->children.push_back(c);
            }
            right_child->children.clear();
        }

        parent->keys.erase(parent->keys.begin() + index);
        parent->values.erase(parent->values.begin() + index);
        parent->children.erase(parent->children.begin() + index + 1);
        
        delete right_child;
    }

    // --- Helpers ---------------------------------------------------------

    template <typename F>
    void in_order_rec(const Node* node, F& visit) const {
        const int limit = node->key_count();
        for (int i = 0; i < limit; ++i) {
            if (!node->is_leaf) {
                in_order_rec(node->children[i], visit);
            }
            visit(node->keys[i], node->values[i]);
        }
        if (!node->is_leaf) {
            in_order_rec(node->children[limit], visit);
        }
    }

    static void print_rec(const Node* node, int depth, std::ostream& os) {
        os << std::string(static_cast<std::size_t>(depth) * 2, ' ') << "[";
        for (int i = 0; i < node->key_count(); ++i) {
            if (i) os << ' ';
            os << node->keys[i];
        }
        os << "]" << (node->is_leaf ? "  (leaf)\n" : "\n");
        for (Node* child : node->children) {
            print_rec(child, depth + 1, os);
        }
    }

    std::string verify_rec(const Node* node, bool is_root, int depth, int& leaf_depth) const {
        const int count = node->key_count();
        
        if (count > 2 * m_min_degree - 1)          return "node holds more than 2t-1 keys";
        if (!is_root && count < m_min_degree - 1)  return "non-root holds fewer than t-1 keys";
        if (is_root && count < 1 && !node->is_leaf) return "non-leaf root has no keys";

        for (int i = 1; i < count; ++i) {
            if (!m_comparator(node->keys[i - 1], node->keys[i])) {
                return "keys not strictly increasing within node";
            }
        }

        if (node->is_leaf) {
            if (leaf_depth == -1) {
                leaf_depth = depth;
            } else if (leaf_depth != depth) {
                return "leaves at different depths";
            }
            
            if (!node->children.empty()) {
                return "leaf has children attached";
            }
            return "";
        }

        if (static_cast<int>(node->children.size()) != count + 1) {
            return "internal node has k != children-1";
        }

        for (int i = 0; i <= count; ++i) {
            const Node* child = node->children[i];
            for (const Key& x : child->keys) {
                if (i < count && !m_comparator(x, node->keys[i])) {
                    return "child key not strictly less than parent separator";
                }
                if (i > 0 && !m_comparator(node->keys[i - 1], x)) {
                    return "child key not strictly greater than parent separator";
                }
            }
            std::string err = verify_rec(child, /*is_root=*/false, depth + 1, leaf_depth);
            if (!err.empty()) return err;
        }
        return "";
    }
};

} // namespace adbms