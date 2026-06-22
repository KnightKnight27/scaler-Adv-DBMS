#ifndef B_TREE_HPP
#define B_TREE_HPP

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <cassert>

namespace adbms {

template <typename Key, typename Value, typename Compare = std::less<Key>>
class BTree {
public:
    struct Node {
        std::vector<Key> keys;
        std::vector<Value> values;
        std::vector<Node*> children;
        bool isLeaf;

        explicit Node(bool leaf) : isLeaf(leaf) {}

        ~Node() {
            for (Node* child : children) {
                delete child;
            }
        }

        bool isFull(int t) const {
            return static_cast<int>(keys.size()) == 2 * t - 1;
        }

        int numKeys() const {
            return static_cast<int>(keys.size());
        }
    };

    explicit BTree(int t = 3) : root(nullptr), t_(t < 2 ? 2 : t), size_(0) {}

    BTree(const BTree&) = delete;
    BTree& operator=(const BTree&) = delete;
    BTree(BTree&&) = delete;
    BTree& operator=(BTree&&) = delete;

    ~BTree() {
        delete root;
    }

    // Insert a key-value pair. Returns true if key is new, false if value is overwritten.
    bool insert(const Key& k, const Value& v) {
        if (!root) {
            root = new Node(true);
            root->keys.push_back(k);
            root->values.push_back(v);
            ++size_;
            return true;
        }

        if (root->isFull(t_)) {
            Node* newRoot = new Node(false);
            newRoot->children.push_back(root);
            splitChild(newRoot, 0);
            root = newRoot;
        }

        return insertNonFull(root, k, v);
    }

    // Erase a key from the B-Tree. Returns true if successfully removed.
    bool erase(const Key& k) {
        if (!root) return false;
        bool removed = eraseFrom(root, k);

        if (root->keys.empty() && !root->isLeaf) {
            Node* oldRoot = root;
            root = root->children[0];
            oldRoot->children.clear(); // Prevent destructor cascade
            delete oldRoot;
        } else if (root->keys.empty() && root->isLeaf) {
            delete root;
            root = nullptr;
        }

        if (removed) --size_;
        return removed;
    }

    // Lookup operations
    bool contains(const Key& k) const {
        return findNode(root, k).first != nullptr;
    }

    Value& at(const Key& k) {
        auto [node, idx] = findNode(root, k);
        if (!node) {
            throw std::out_of_range("BTree::at - key not found");
        }
        return node->values[idx];
    }

    const Value& at(const Key& k) const {
        auto [node, idx] = findNode(root, k);
        if (!node) {
            throw std::out_of_range("BTree::at - key not found");
        }
        return node->values[idx];
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    int degree() const { return t_; }

    // In-order traversal
    template <typename Func>
    void inOrder(Func&& visit) const {
        if (root) {
            inOrderRec(root, visit);
        }
    }

    // Visual print structure
    void print(std::ostream& os = std::cout) const {
        if (!root) {
            os << "<empty>\n";
            return;
        }
        printRec(root, 0, os);
    }

    // Invariant verifier - returns empty string on success, else the error details
    std::string verify() const {
        if (!root) return "";
        int leafDepth = -1;
        return verifyRec(root, true, 0, leafDepth);
    }

private:
    Node* root;
    int t_;
    std::size_t size_;
    Compare cmp_{};

    // Find the slot index `i` such that keys[i-1] < k <= keys[i]
    int lowerBoundIn(const Node* node, const Key& k) const {
        int idx = 0;
        int num = node->numKeys();
        while (idx < num && cmp_(node->keys[idx], k)) {
            ++idx;
        }
        return idx;
    }

    std::pair<Node*, int> findNode(Node* node, const Key& k) const {
        Node* cur = node;
        while (cur) {
            int idx = lowerBoundIn(cur, k);
            if (idx < cur->numKeys() && !cmp_(k, cur->keys[idx])) {
                return {cur, idx};
            }
            if (cur->isLeaf) {
                return {nullptr, 0};
            }
            cur = cur->children[idx];
        }
        return {nullptr, 0};
    }

    // Split a child node that is full
    void splitChild(Node* parent, int childPos) {
        Node* fullNode = parent->children[childPos];
        Node* sibling = new Node(fullNode->isLeaf);

        // Move upper t-1 keys & values to sibling
        sibling->keys.assign(fullNode->keys.begin() + t_, fullNode->keys.end());
        sibling->values.assign(fullNode->values.begin() + t_, fullNode->values.end());

        if (!fullNode->isLeaf) {
            sibling->children.assign(fullNode->children.begin() + t_, fullNode->children.end());
            fullNode->children.erase(fullNode->children.begin() + t_, fullNode->children.end());
        }

        // Promote the median key & value to parent
        Key medianKey = std::move(fullNode->keys[t_ - 1]);
        Value medianValue = std::move(fullNode->values[t_ - 1]);

        fullNode->keys.erase(fullNode->keys.begin() + (t_ - 1), fullNode->keys.end());
        fullNode->values.erase(fullNode->values.begin() + (t_ - 1), fullNode->values.end());

        parent->children.insert(parent->children.begin() + childPos + 1, sibling);
        parent->keys.insert(parent->keys.begin() + childPos, std::move(medianKey));
        parent->values.insert(parent->values.begin() + childPos, std::move(medianValue));
    }

    // Insert key-value pair when node is guaranteed non-full
    bool insertNonFull(Node* node, const Key& k, const Value& v) {
        int idx = node->numKeys() - 1;

        if (node->isLeaf) {
            int slot = lowerBoundIn(node, k);
            if (slot < node->numKeys() && !cmp_(k, node->keys[slot])) {
                node->values[slot] = v;
                return false; // Value overwritten
            }
            node->keys.insert(node->keys.begin() + slot, k);
            node->values.insert(node->values.begin() + slot, v);
            ++size_;
            return true;
        }

        while (idx >= 0 && cmp_(k, node->keys[idx])) {
            --idx;
        }
        if (idx >= 0 && !cmp_(node->keys[idx], k)) {
            node->values[idx] = v;
            return false; // Overwritten inside internal node
        }
        ++idx;

        if (node->children[idx]->isFull(t_)) {
            splitChild(node, idx);
            if (cmp_(node->keys[idx], k)) {
                ++idx;
            } else if (!cmp_(k, node->keys[idx])) {
                node->values[idx] = v;
                return false;
            }
        }
        return insertNonFull(node->children[idx], k, v);
    }

    // Erase algorithm
    bool eraseFrom(Node* node, const Key& k) {
        int idx = lowerBoundIn(node, k);
        bool foundHere = (idx < node->numKeys()) && !cmp_(k, node->keys[idx]);

        if (foundHere && node->isLeaf) {
            node->keys.erase(node->keys.begin() + idx);
            node->values.erase(node->values.begin() + idx);
            return true;
        }

        if (foundHere) {
            return eraseInternal(node, idx);
        }

        if (node->isLeaf) return false;

        bool isLastChild = (idx == node->numKeys());
        if (node->children[idx]->numKeys() < t_) {
            ensureMinKeys(node, idx);
        }

        if (isLastChild && idx > node->numKeys()) {
            --idx;
        }
        return eraseFrom(node->children[idx], k);
    }

    bool eraseInternal(Node* node, int idx) {
        Node* left = node->children[idx];
        Node* right = node->children[idx + 1];

        if (left->numKeys() >= t_) {
            auto [pk, pv] = popMax(left);
            node->keys[idx] = std::move(pk);
            node->values[idx] = std::move(pv);
            return true;
        }

        if (right->numKeys() >= t_) {
            auto [sk, sv] = popMin(right);
            node->keys[idx] = std::move(sk);
            node->values[idx] = std::move(sv);
            return true;
        }

        // Sibling nodes are minimal, merge key down
        mergeChildren(node, idx);
        Key target = left->keys[t_ - 1];
        return eraseFrom(left, target);
    }

    std::pair<Key, Value> popMin(Node* node) {
        if (node->isLeaf) {
            Key k = std::move(node->keys.front());
            Value v = std::move(node->values.front());
            node->keys.erase(node->keys.begin());
            node->values.erase(node->values.begin());
            return {std::move(k), std::move(v)};
        }
        if (node->children[0]->numKeys() < t_) {
            ensureMinKeys(node, 0);
        }
        return popMin(node->children[0]);
    }

    std::pair<Key, Value> popMax(Node* node) {
        if (node->isLeaf) {
            Key k = std::move(node->keys.back());
            Value v = std::move(node->values.back());
            node->keys.pop_back();
            node->values.pop_back();
            return {std::move(k), std::move(v)};
        }
        int lastIdx = node->numKeys();
        if (node->children[lastIdx]->numKeys() < t_) {
            ensureMinKeys(node, lastIdx);
        }
        return popMax(node->children[node->numKeys()]);
    }

    void ensureMinKeys(Node* parent, int childPos) {
        Node* child = parent->children[childPos];
        if (child->numKeys() >= t_) return;

        Node* leftSib = (childPos > 0) ? parent->children[childPos - 1] : nullptr;
        Node* rightSib = (childPos < parent->numKeys()) ? parent->children[childPos + 1] : nullptr;

        if (leftSib && leftSib->numKeys() >= t_) {
            // Borrow from left sibling
            child->keys.insert(child->keys.begin(), std::move(parent->keys[childPos - 1]));
            child->values.insert(child->values.begin(), std::move(parent->values[childPos - 1]));

            parent->keys[childPos - 1] = std::move(leftSib->keys.back());
            parent->values[childPos - 1] = std::move(leftSib->values.back());

            leftSib->keys.pop_back();
            leftSib->values.pop_back();

            if (!child->isLeaf) {
                child->children.insert(child->children.begin(), leftSib->children.back());
                leftSib->children.pop_back();
            }
            return;
        }

        if (rightSib && rightSib->numKeys() >= t_) {
            // Borrow from right sibling
            child->keys.push_back(std::move(parent->keys[childPos]));
            child->values.push_back(std::move(parent->values[childPos]));

            parent->keys[childPos] = std::move(rightSib->keys.front());
            parent->values[childPos] = std::move(rightSib->values.front());

            rightSib->keys.erase(rightSib->keys.begin());
            rightSib->values.erase(rightSib->values.begin());

            if (!child->isLeaf) {
                child->children.push_back(rightSib->children.front());
                rightSib->children.erase(rightSib->children.begin());
            }
            return;
        }

        // Merge siblings
        if (rightSib) {
            mergeChildren(parent, childPos);
        } else {
            mergeChildren(parent, childPos - 1);
        }
    }

    void mergeChildren(Node* parent, int childPos) {
        Node* left = parent->children[childPos];
        Node* right = parent->children[childPos + 1];

        left->keys.push_back(std::move(parent->keys[childPos]));
        left->values.push_back(std::move(parent->values[childPos]));

        for (auto& k : right->keys) left->keys.push_back(std::move(k));
        for (auto& v : right->values) left->values.push_back(std::move(v));

        if (!left->isLeaf) {
            for (Node* c : right->children) left->children.push_back(c);
            right->children.clear();
        }

        parent->keys.erase(parent->keys.begin() + childPos);
        parent->values.erase(parent->values.begin() + childPos);
        parent->children.erase(parent->children.begin() + childPos + 1);
        delete right;
    }

    template <typename Func>
    void inOrderRec(const Node* node, Func& visit) const {
        int num = node->numKeys();
        for (int i = 0; i < num; ++i) {
            if (!node->isLeaf) {
                inOrderRec(node->children[i], visit);
            }
            visit(node->keys[i], node->values[i]);
        }
        if (!node->isLeaf) {
            inOrderRec(node->children[num], visit);
        }
    }

    static void printRec(const Node* node, int depth, std::ostream& os) {
        os << std::string(static_cast<std::size_t>(depth) * 4, ' ') << "[";
        for (int i = 0; i < node->numKeys(); ++i) {
            if (i > 0) os << ' ';
            os << node->keys[i];
        }
        os << "]" << (node->isLeaf ? " (leaf)\n" : "\n");
        for (Node* child : node->children) {
            printRec(child, depth + 1, os);
        }
    }

    std::string verifyRec(const Node* node, bool isRoot, int depth, int& leafDepth) const {
        const int num = node->numKeys();
        if (num > 2 * t_ - 1) return "Node exceeds maximum keys (2t-1)";
        if (!isRoot && num < t_ - 1) return "Non-root node has fewer than t-1 keys";
        if (isRoot && num < 1 && !node->isLeaf) return "Non-leaf root has no keys";

        for (int i = 1; i < num; ++i) {
            if (!cmp_(node->keys[i - 1], node->keys[i])) {
                return "Keys are not in strictly increasing order";
            }
        }

        if (node->isLeaf) {
            if (leafDepth == -1) {
                leafDepth = depth;
            } else if (leafDepth != depth) {
                return "Leaf nodes are at different depths";
            }
            if (!node->children.empty()) return "Leaf node has children attached";
            return "";
        }

        if (static_cast<int>(node->children.size()) != num + 1) {
            return "Internal node has child count != key count + 1";
        }

        for (int i = 0; i <= num; ++i) {
            const Node* child = node->children[i];
            for (const Key& childKey : child->keys) {
                if (i < num && !cmp_(childKey, node->keys[i])) {
                    return "Child key exceeds parent separator";
                }
                if (i > 0 && !cmp_(node->keys[i - 1], childKey)) {
                    return "Child key is below parent separator";
                }
            }
            std::string err = verifyRec(child, false, depth + 1, leafDepth);
            if (!err.empty()) return err;
        }

        return "";
    }
};

} // namespace adbms

#endif // B_TREE_HPP
