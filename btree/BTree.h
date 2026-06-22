#ifndef BTREE_H
#define BTREE_H

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

template <typename T>
class BTreeNode {
public:
    BTreeNode(int t, bool leaf)
        : t(t), leaf(leaf) {
        keys.reserve(2 * t - 1);
        children.reserve(2 * t);
    }

    ~BTreeNode() {
        for (auto child : children) {
            delete child;
        }
    }

    void traverse() const {
        size_t i = 0;
        for (; i < keys.size(); ++i) {
            if (!leaf) {
                children[i]->traverse();
            }
            std::cout << keys[i] << " ";
        }
        if (!leaf) {
            children[i]->traverse();
        }
    }

    BTreeNode<T>* search(const T& key) {
        size_t i = 0;
        while (i < keys.size() && key > keys[i]) {
            ++i;
        }
        if (i < keys.size() && keys[i] == key) {
            return this;
        }
        if (leaf) {
            return nullptr;
        }
        return children[i]->search(key);
    }

    void insertNonFull(const T& key) {
        int i = static_cast<int>(keys.size()) - 1;

        if (leaf) {
            keys.emplace_back();
            while (i >= 0 && keys[i] > key) {
                keys[i + 1] = keys[i];
                --i;
            }
            keys[i + 1] = key;
        } else {
            while (i >= 0 && keys[i] > key) {
                --i;
            }
            ++i;
            if (children[i]->keys.size() == 2 * t - 1) {
                splitChild(i, children[i]);
                if (keys[i] < key) {
                    ++i;
                }
            }
            children[i]->insertNonFull(key);
        }
    }

    void splitChild(int i, BTreeNode<T>* y) {
        auto* z = new BTreeNode<T>(y->t, y->leaf);
        z->keys.assign(y->keys.begin() + t, y->keys.end());

        if (!y->leaf) {
            z->children.assign(y->children.begin() + t, y->children.end());
        }

        T median = y->keys[t - 1];

        y->keys.erase(y->keys.begin() + t - 1, y->keys.end());
        if (!y->leaf) {
            y->children.erase(y->children.begin() + t, y->children.end());
        }

        children.insert(children.begin() + i + 1, z);
        keys.insert(keys.begin() + i, median);
    }

    std::vector<T> keys;
    std::vector<BTreeNode<T>*> children;
    bool leaf;
    int t;
};

template <typename T>
class BTree {
public:
    explicit BTree(int degree)
        : root(nullptr), t(degree) {
    }

    ~BTree() {
        delete root;
    }

    void traverse() const {
        if (root) {
            root->traverse();
            std::cout << "\n";
        }
    }

    bool search(const T& key) const {
        return root ? root->search(key) != nullptr : false;
    }

    void insert(const T& key) {
        if (!root) {
            root = new BTreeNode<T>(t, true);
            root->keys.push_back(key);
            return;
        }

        if (root->keys.size() == static_cast<size_t>(2 * t - 1)) {
            auto* s = new BTreeNode<T>(t, false);
            s->children.push_back(root);
            s->splitChild(0, root);

            int i = 0;
            if (s->keys[0] < key) {
                ++i;
            }
            s->children[i]->insertNonFull(key);
            root = s;
        } else {
            root->insertNonFull(key);
        }
    }

private:
    BTreeNode<T>* root;
    int t;
};

#endif // BTREE_H
