#ifndef B_TREE_HPP
#define B_TREE_HPP

#include <vector>
#include <utility>
#include <functional>
#include <stdexcept>
#include <string>
#include <iostream>

namespace adbms {

template <typename Key, typename Value, typename Compare = std::less<Key>>
class BTree {
public:
    struct Node {
        bool isLeaf;
        std::vector<Key> keys;
        std::vector<Value> values;
        std::vector<Node*> children;

        Node(bool leaf) : isLeaf(leaf) {}
        ~Node() {
            for (Node* child : children) {
                delete child;
            }
        }
        int numKeys() const { return keys.size(); }
    };

private:
    Node* root_;
    int t_; // Minimum degree
    Compare cmp_;

public:
    explicit BTree(int t) : root_(nullptr), t_(t) {
        if (t < 2) throw std::invalid_argument("Minimum degree must be >= 2");
    }

    ~BTree() {
        delete root_;
    }

    int size() const {
        int count = 0;
        inOrder([&count](const Key&, const Value&) { ++count; });
        return count;
    }

    bool empty() const {
        return root_ == nullptr || root_->keys.empty();
    }

    bool find(const Key& k, Value& v) const {
        if (!root_) return false;
        return findIn(root_, k, v);
    }

    Value& at(const Key& k) {
        if (!root_) throw std::out_of_range("BTree::at - key not found");
        return findOrThrow(root_, k);
    }

    bool insert(const Key& k, const Value& v) {
        if (!root_) {
            root_ = new Node(true);
            root_->keys.push_back(k);
            root_->values.push_back(v);
            return true;
        }

        // Proactive split of root if full
        if (root_->numKeys() == 2 * t_ - 1) {
            Node* s = new Node(false);
            s->children.push_back(root_);
            splitChild(s, 0, root_);
            root_ = s;
        }

        return insertNonFull(root_, k, v);
    }

    bool erase(const Key& k) {
        if (!root_ || root_->keys.empty()) return false;

        bool result = eraseFrom(root_, k);

        if (root_->keys.empty() && !root_->isLeaf) {
            Node* oldRoot = root_;
            root_ = root_->children[0];
            oldRoot->children.clear();
            delete oldRoot;
        }
        return result;
    }

    template <typename Func>
    void inOrder(Func visit) const {
        if (root_) inOrderRec(root_, visit);
    }

    void print(std::ostream& os = std::cout) const {
        if (root_) printRec(root_, 0, os);
    }

    std::string verify() const {
        if (!root_) return "";
        int leafDepth = -1;
        return verifyRec(root_, true, 0, leafDepth);
    }

private:
    bool findIn(const Node* node, const Key& k, Value& v) const {
        int idx = 0;
        int num = node->numKeys();
        while (idx < num && cmp_(node->keys[idx], k)) {
            ++idx;
        }
        if (idx < num && !cmp_(k, node->keys[idx])) {
            v = node->values[idx];
            return true;
        }
        if (node->isLeaf) return false;
        return findIn(node->children[idx], k, v);
    }

    Value& findOrThrow(Node* node, const Key& k) {
        int idx = 0;
        int num = node->numKeys();
        while (idx < num && cmp_(node->keys[idx], k)) {
            ++idx;
        }
        if (idx < num && !cmp_(k, node->keys[idx])) {
            return node->values[idx];
        }
        if (node->isLeaf) throw std::out_of_range("BTree::at - key not found");
        return findOrThrow(node->children[idx], k);
    }

    int lowerBoundIn(const Node* node, const Key& k) const {
        int idx = 0;
        int num = node->numKeys();
        while (idx < num && cmp_(node->keys[idx], k)) {
            ++idx;
        }
        return idx;
    }

    void splitChild(Node* parent, int i, Node* y) {
        Node* z = new Node(y->isLeaf);
        
        // Split keys & values
        z->keys.assign(y->keys.begin() + t_, y->keys.end());
        z->values.assign(y->values.begin() + t_, y->values.end());

        // Split children if not leaf
        if (!y->isLeaf) {
            z->children.assign(y->children.begin() + t_, y->children.end());
            y->children.resize(t_);
        }

        Key promotedKey = y->keys[t_ - 1];
        Value promotedVal = y->values[t_ - 1];

        y->keys.resize(t_ - 1);
        y->values.resize(t_ - 1);

        parent->children.insert(parent->children.begin() + i + 1, z);
        parent->keys.insert(parent->keys.begin() + i, promotedKey);
        parent->values.insert(parent->values.begin() + i, promotedVal);
    }

    bool insertNonFull(Node* node, const Key& k, const Value& v) {
        int idx = node->numKeys() - 1;

        if (node->isLeaf) {
            while (idx >= 0 && cmp_(k, node->keys[idx])) {
                --idx;
            }
            if (idx >= 0 && !cmp_(k, node->keys[idx]) && !cmp_(node->keys[idx], k)) {
                node->values[idx] = v; // Overwrite
                return false;
            }
            node->keys.insert(node->keys.begin() + idx + 1, k);
            node->values.insert(node->values.begin() + idx + 1, v);
            return true;
        }

        while (idx >= 0 && cmp_(k, node->keys[idx])) {
            --idx;
        }
        ++idx;

        if (node->children[idx]->numKeys() == 2 * t_ - 1) {
            splitChild(node, idx, node->children[idx]);
            if (cmp_(node->keys[idx], k)) {
                ++idx;
            } else if (!cmp_(k, node->keys[idx])) {
                node->values[idx] = v;
                return false;
            }
        }
        return insertNonFull(node->children[idx], k, v);
    }

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

    // Overloaded helper for recursion inside erase
    bool eraseFromWithTarget(Node* node, const Key& k) {
        return eraseFrom(node, k);
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
