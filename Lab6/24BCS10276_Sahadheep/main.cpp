#include <iostream>
#include <vector>

class BTreeNode {
public:
    explicit BTreeNode(int t, bool leaf)
        : t_(t), leaf_(leaf), keys_(), children_() {
        keys_.reserve(2 * t_ - 1);
        children_.reserve(2 * t_);
    }

    void traverse() const {
        int i = 0;
        for (; i < static_cast<int>(keys_.size()); ++i) {
            if (!leaf_) {
                children_[i]->traverse();
            }
            std::cout << keys_[i] << " ";
        }
        if (!leaf_) {
            children_[i]->traverse();
        }
    }

    BTreeNode* search(int key) {
        int i = 0;
        while (i < static_cast<int>(keys_.size()) && key > keys_[i]) {
            ++i;
        }
        if (i < static_cast<int>(keys_.size()) && keys_[i] == key) {
            return this;
        }
        if (leaf_) {
            return nullptr;
        }
        return children_[i]->search(key);
    }

    void insertNonFull(int key) {
        int i = static_cast<int>(keys_.size()) - 1;
        if (leaf_) {
            keys_.push_back(0);
            while (i >= 0 && key < keys_[i]) {
                keys_[i + 1] = keys_[i];
                --i;
            }
            keys_[i + 1] = key;
        } else {
            while (i >= 0 && key < keys_[i]) {
                --i;
            }
            ++i;
            if (children_[i]->isFull()) {
                splitChild(i, children_[i]);
                if (key > keys_[i]) {
                    ++i;
                }
            }
            children_[i]->insertNonFull(key);
        }
    }

    void splitChild(int index, BTreeNode* child) {
        BTreeNode* sibling = new BTreeNode(child->t_, child->leaf_);

        for (int j = 0; j < t_ - 1; ++j) {
            sibling->keys_.push_back(child->keys_[j + t_]);
        }

        if (!child->leaf_) {
            for (int j = 0; j < t_; ++j) {
                sibling->children_.push_back(child->children_[j + t_]);
            }
        }

        int median = child->keys_[t_ - 1];
        child->keys_.resize(t_ - 1);
        if (!child->leaf_) {
            child->children_.resize(t_);
        }

        children_.insert(children_.begin() + index + 1, sibling);
        keys_.insert(keys_.begin() + index, median);
    }

    bool isFull() const {
        return static_cast<int>(keys_.size()) == 2 * t_ - 1;
    }

private:
    int t_;
    bool leaf_;
    std::vector<int> keys_;
    std::vector<BTreeNode*> children_;

    friend class BTree;
};

class BTree {
public:
    explicit BTree(int t) : t_(t), root_(nullptr) {}

    void traverse() const {
        if (root_) {
            root_->traverse();
        }
    }

    BTreeNode* search(int key) {
        if (!root_) {
            return nullptr;
        }
        return root_->search(key);
    }

    void insert(int key) {
        if (!root_) {
            root_ = new BTreeNode(t_, true);
            root_->keys_.push_back(key);
            return;
        }

        if (root_->isFull()) {
            BTreeNode* newRoot = new BTreeNode(t_, false);
            newRoot->children_.push_back(root_);
            newRoot->splitChild(0, root_);

            int i = 0;
            if (key > newRoot->keys_[0]) {
                i = 1;
            }
            newRoot->children_[i]->insertNonFull(key);
            root_ = newRoot;
        } else {
            root_->insertNonFull(key);
        }
    }

private:
    int t_;
    BTreeNode* root_;
};

int main() {
    BTree tree(3);
    std::vector<int> values = {10, 20, 5, 6, 12, 30, 7, 17};
    for (int value : values) {
        tree.insert(value);
    }

    std::cout << "B-Tree traversal: ";
    tree.traverse();
    std::cout << "\n";

    int searchKey = 6;
    std::cout << "Search " << searchKey << ": "
              << (tree.search(searchKey) ? "found" : "not found") << "\n";

    return 0;
}