#include <algorithm>
#include <iostream>
#include <vector>

class BTree {
    struct Page {
        explicit Page(bool is_leaf) : leaf(is_leaf) {}

        ~Page() {
            for (Page* child : children) {
                delete child;
            }
        }

        bool leaf = true;
        std::vector<int> keys;
        std::vector<Page*> children;
    };

public:
    explicit BTree(int min_degree = 3) : t_(min_degree), root_(new Page(true)) {}

    ~BTree() {
        delete root_;
    }

    bool contains(int key) const {
        return contains(root_, key);
    }

    void insert(int key) {
        if (contains(key)) {
            return;
        }

        if (full(root_)) {
            Page* new_root = new Page(false);
            new_root->children.push_back(root_);
            root_ = new_root;
            split_child(root_, 0);
        }
        insert_nonfull(root_, key);
    }

    void erase(int key) {
        if (root_->keys.empty() && root_->leaf) {
            return;
        }

        erase(root_, key);
        if (!root_->leaf && root_->keys.empty()) {
            Page* old_root = root_;
            root_ = root_->children.front();
            old_root->children.clear();
            delete old_root;
        }
    }

    void print() const {
        print(root_);
        std::cout << '\n';
    }

private:
    int t_;
    Page* root_;

    bool full(Page* page) const {
        return static_cast<int>(page->keys.size()) == 2 * t_ - 1;
    }

    int lower_slot(Page* page, int key) const {
        return static_cast<int>(std::lower_bound(page->keys.begin(), page->keys.end(), key) -
                                page->keys.begin());
    }

    bool contains(Page* page, int key) const {
        int slot = lower_slot(page, key);
        if (slot < static_cast<int>(page->keys.size()) && page->keys[slot] == key) {
            return true;
        }
        if (page->leaf) {
            return false;
        }
        return contains(page->children[slot], key);
    }

    void split_child(Page* parent, int slot) {
        Page* left = parent->children[slot];
        Page* right = new Page(left->leaf);
        const int promoted = left->keys[t_ - 1];

        right->keys.assign(left->keys.begin() + t_, left->keys.end());
        left->keys.resize(t_ - 1);

        if (!left->leaf) {
            right->children.assign(left->children.begin() + t_, left->children.end());
            left->children.resize(t_);
        }

        parent->keys.insert(parent->keys.begin() + slot, promoted);
        parent->children.insert(parent->children.begin() + slot + 1, right);
    }

    void insert_nonfull(Page* page, int key) {
        int slot = lower_slot(page, key);
        if (page->leaf) {
            page->keys.insert(page->keys.begin() + slot, key);
            return;
        }

        if (full(page->children[slot])) {
            split_child(page, slot);
            if (key > page->keys[slot]) {
                ++slot;
            }
        }
        insert_nonfull(page->children[slot], key);
    }

    int predecessor(Page* page) const {
        while (!page->leaf) {
            page = page->children.back();
        }
        return page->keys.back();
    }

    int successor(Page* page) const {
        while (!page->leaf) {
            page = page->children.front();
        }
        return page->keys.front();
    }

    void erase(Page* page, int key) {
        int slot = lower_slot(page, key);

        if (slot < static_cast<int>(page->keys.size()) && page->keys[slot] == key) {
            if (page->leaf) {
                page->keys.erase(page->keys.begin() + slot);
            } else {
                erase_internal_key(page, slot);
            }
            return;
        }

        if (page->leaf) {
            return;
        }

        bool was_right_edge = slot == static_cast<int>(page->keys.size());
        if (static_cast<int>(page->children[slot]->keys.size()) == t_ - 1) {
            fill_child(page, slot);
        }

        if (was_right_edge && slot > static_cast<int>(page->keys.size())) {
            erase(page->children[slot - 1], key);
        } else {
            erase(page->children[slot], key);
        }
    }

    void erase_internal_key(Page* page, int slot) {
        int key = page->keys[slot];
        Page* left = page->children[slot];
        Page* right = page->children[slot + 1];

        if (static_cast<int>(left->keys.size()) >= t_) {
            int pred = predecessor(left);
            page->keys[slot] = pred;
            erase(left, pred);
        } else if (static_cast<int>(right->keys.size()) >= t_) {
            int succ = successor(right);
            page->keys[slot] = succ;
            erase(right, succ);
        } else {
            merge_children(page, slot);
            erase(left, key);
        }
    }

    void fill_child(Page* page, int slot) {
        if (slot > 0 && static_cast<int>(page->children[slot - 1]->keys.size()) >= t_) {
            borrow_from_left(page, slot);
        } else if (slot + 1 < static_cast<int>(page->children.size()) &&
                   static_cast<int>(page->children[slot + 1]->keys.size()) >= t_) {
            borrow_from_right(page, slot);
        } else if (slot + 1 < static_cast<int>(page->children.size())) {
            merge_children(page, slot);
        } else {
            merge_children(page, slot - 1);
        }
    }

    void borrow_from_left(Page* page, int slot) {
        Page* child = page->children[slot];
        Page* sibling = page->children[slot - 1];

        child->keys.insert(child->keys.begin(), page->keys[slot - 1]);
        page->keys[slot - 1] = sibling->keys.back();
        sibling->keys.pop_back();

        if (!sibling->leaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }
    }

    void borrow_from_right(Page* page, int slot) {
        Page* child = page->children[slot];
        Page* sibling = page->children[slot + 1];

        child->keys.push_back(page->keys[slot]);
        page->keys[slot] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());

        if (!sibling->leaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }
    }

    void merge_children(Page* page, int slot) {
        Page* left = page->children[slot];
        Page* right = page->children[slot + 1];

        left->keys.push_back(page->keys[slot]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        left->children.insert(left->children.end(), right->children.begin(), right->children.end());
        right->children.clear();

        page->keys.erase(page->keys.begin() + slot);
        page->children.erase(page->children.begin() + slot + 1);
        delete right;
    }

    void print(Page* page) const {
        for (std::size_t i = 0; i < page->keys.size(); ++i) {
            if (!page->leaf) {
                print(page->children[i]);
            }
            std::cout << page->keys[i] << ' ';
        }
        if (!page->leaf) {
            print(page->children.back());
        }
    }
};

int main() {
    BTree tree(3);
    for (int key : {40, 10, 70, 20, 90, 30, 80, 50, 60, 100, 5, 15, 35}) {
        tree.insert(key);
    }

    std::cout << "after inserts: ";
    tree.print();
    std::cout << "contains 60: " << (tree.contains(60) ? "yes" : "no") << '\n';
    std::cout << "contains 99: " << (tree.contains(99) ? "yes" : "no") << '\n';

    for (int key : {10, 70, 40, 5}) {
        tree.erase(key);
    }
    std::cout << "after deletes: ";
    tree.print();
}
