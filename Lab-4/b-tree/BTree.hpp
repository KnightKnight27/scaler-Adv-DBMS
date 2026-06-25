#pragma once

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

class BTree {
public:
    explicit BTree(int min_degree) : root_(nullptr), t_(min_degree) {
        if (t_ < 2) {
            throw std::invalid_argument("BTree minimum degree must be at least 2");
        }
    }

    ~BTree() { delete root_; }

    void insert(int key) {
        if (root_ == nullptr) {
            root_ = new Node(t_, true);
            root_->keys.push_back(key);
            return;
        }

        if (root_->keys.size() == maxKeys()) {
            Node *new_root = new Node(t_, false);
            new_root->children.push_back(root_);
            splitChild(new_root, 0);
            int index = 0;
            if (new_root->keys[0] < key) {
                index = 1;
            }
            insertNonFull(new_root->children[index], key);
            root_ = new_root;
            return;
        }

        insertNonFull(root_, key);
    }

    void remove(int key) {
        if (root_ == nullptr) {
            return;
        }

        remove(root_, key);

        if (root_ != nullptr && root_->keys.empty()) {
            if (root_->leaf) {
                delete root_;
                root_ = nullptr;
            } else {
                Node *old_root = root_;
                root_ = root_->children.front();
                old_root->children[0] = nullptr;
                delete old_root;
            }
        }
    }

    bool contains(int key) const { return search(root_, key) != nullptr; }

    void printInOrder(std::ostream &out) const {
        printInOrder(root_, out);
        out << '\n';
    }

private:
    struct Node {
        explicit Node(int degree, bool is_leaf) : leaf(is_leaf), t(degree) {}

        ~Node() {
            for (Node *child : children) {
                delete child;
            }
        }

        bool leaf;
        int t;
        std::vector<int> keys;
        std::vector<Node *> children;
    };

    Node *root_;
    int t_;

    int maxKeys() const { return 2 * t_ - 1; }

    Node *search(Node *node, int key) const {
        if (node == nullptr) {
            return nullptr;
        }

        std::size_t index = 0;
        while (index < node->keys.size() && key > node->keys[index]) {
            ++index;
        }

        if (index < node->keys.size() && node->keys[index] == key) {
            return node;
        }

        if (node->leaf) {
            return nullptr;
        }

        return search(node->children[index], key);
    }

    void printInOrder(Node *node, std::ostream &out) const {
        if (node == nullptr) {
            return;
        }

        for (std::size_t i = 0; i < node->keys.size(); ++i) {
            if (!node->leaf) {
                printInOrder(node->children[i], out);
            }
            out << node->keys[i] << ' ';
        }

        if (!node->leaf) {
            printInOrder(node->children.back(), out);
        }
    }

    void splitChild(Node *parent, std::size_t index) {
        Node *full_child = parent->children[index];
        Node *new_child = new Node(t_, full_child->leaf);

        const int middle_key = full_child->keys[t_ - 1];

        new_child->keys.assign(full_child->keys.begin() + t_, full_child->keys.end());
        full_child->keys.resize(t_ - 1);

        if (!full_child->leaf) {
            new_child->children.assign(full_child->children.begin() + t_, full_child->children.end());
            full_child->children.resize(t_);
        }

        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index + 1), new_child);
        parent->keys.insert(parent->keys.begin() + static_cast<std::ptrdiff_t>(index), middle_key);
    }

    void insertNonFull(Node *node, int key) {
        std::size_t index = node->keys.size();

        if (node->leaf) {
            node->keys.push_back(key);
            while (index > 0 && node->keys[index - 1] > key) {
                node->keys[index] = node->keys[index - 1];
                --index;
            }
            node->keys[index] = key;
            return;
        }

        while (index > 0 && key < node->keys[index - 1]) {
            --index;
        }

        if (node->children[index]->keys.size() == maxKeys()) {
            splitChild(node, index);
            if (key > node->keys[index]) {
                ++index;
            }
        }

        insertNonFull(node->children[index], key);
    }

    int getPredecessor(Node *node) const {
        Node *current = node;
        while (!current->leaf) {
            current = current->children.back();
        }
        return current->keys.back();
    }

    int getSuccessor(Node *node) const {
        Node *current = node;
        while (!current->leaf) {
            current = current->children.front();
        }
        return current->keys.front();
    }

    void borrowFromPrev(Node *node, std::size_t index) {
        Node *child = node->children[index];
        Node *sibling = node->children[index - 1];

        child->keys.insert(child->keys.begin(), node->keys[index - 1]);

        if (!child->leaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }

        node->keys[index - 1] = sibling->keys.back();
        sibling->keys.pop_back();
    }

    void borrowFromNext(Node *node, std::size_t index) {
        Node *child = node->children[index];
        Node *sibling = node->children[index + 1];

        child->keys.push_back(node->keys[index]);

        if (!child->leaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }

        node->keys[index] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());
    }

    void merge(Node *node, std::size_t index) {
        Node *child = node->children[index];
        Node *sibling = node->children[index + 1];

        child->keys.push_back(node->keys[index]);
        child->keys.insert(child->keys.end(), sibling->keys.begin(), sibling->keys.end());

        if (!child->leaf) {
            child->children.insert(child->children.end(), sibling->children.begin(), sibling->children.end());
            sibling->children.clear();
        }

        node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(index));
        node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(index + 1));
        sibling->children.clear();
        delete sibling;
    }

    void fill(Node *node, std::size_t index) {
        if (index > 0 && node->children[index - 1]->keys.size() >= static_cast<std::size_t>(t_)) {
            borrowFromPrev(node, index);
        } else if (index < node->children.size() - 1 && node->children[index + 1]->keys.size() >= static_cast<std::size_t>(t_)) {
            borrowFromNext(node, index);
        } else {
            if (index < node->children.size() - 1) {
                merge(node, index);
            } else {
                merge(node, index - 1);
            }
        }
    }

    void remove(Node *node, int key) {
        std::size_t index = 0;
        while (index < node->keys.size() && node->keys[index] < key) {
            ++index;
        }

        if (index < node->keys.size() && node->keys[index] == key) {
            if (node->leaf) {
                removeFromLeaf(node, index);
            } else {
                removeFromNonLeaf(node, index);
            }
            return;
        }

        if (node->leaf) {
            return;
        }

        const bool last_child = (index == node->keys.size());
        if (node->children.size() > 1 && node->children[index]->keys.size() == static_cast<std::size_t>(t_ - 1)) {
            fill(node, index);
        }

        if (last_child && index > node->keys.size()) {
            remove(node->children[index - 1], key);
        } else {
            remove(node->children[index], key);
        }
    }

    void removeFromLeaf(Node *node, std::size_t index) {
        node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void removeFromNonLeaf(Node *node, std::size_t index) {
        int key = node->keys[index];

        if (node->children[index]->keys.size() >= static_cast<std::size_t>(t_)) {
            const int predecessor = getPredecessor(node->children[index]);
            node->keys[index] = predecessor;
            remove(node->children[index], predecessor);
        } else if (node->children[index + 1]->keys.size() >= static_cast<std::size_t>(t_)) {
            const int successor = getSuccessor(node->children[index + 1]);
            node->keys[index] = successor;
            remove(node->children[index + 1], successor);
        } else {
            merge(node, index);
            remove(node->children[index], key);
        }
    }
};