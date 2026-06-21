// Lab 6 - B-Tree index
// Akshat Kushwaha | 24BCS10060
//
// A B-Tree of minimum degree t. This is the structure real databases use for
// on-disk indexes (one node = one disk page). Compared to the red-black tree
// from Lab 5, a B-Tree node holds MANY keys (up to 2t-1) instead of one, so the
// tree is very short and a lookup touches very few nodes/pages.
//
// Rules (for minimum degree t):
//   - every node has at most 2t-1 keys
//   - every node except the root has at least t-1 keys
//   - a node with k keys has k+1 children (unless it is a leaf)
//   - all leaves sit at the same depth
//
// Supports insert (split-on-the-way-down), search, delete (borrow/merge), and
// a level-by-level printer so the shape is visible.
//
// Build: g++ -std=c++17 -Wall -Wextra btree.cpp -o btree
// Run:   ./btree

#include <iostream>
#include <vector>

class BTree {
public:
    explicit BTree(int t) : t_(t < 2 ? 2 : t), root_(nullptr) {}
    ~BTree() { destroy(root_); }

    void insert(int key) {
        if (root_ == nullptr) {
            root_ = new Node(true);
            root_->keys.push_back(key);
            return;
        }
        // If the root is full, grow upward first (this is the only way the
        // tree gets taller).
        if (full(root_)) {
            Node* fresh = new Node(false);
            fresh->kids.push_back(root_);
            split_child(fresh, 0);
            root_ = fresh;
        }
        insert_nonfull(root_, key);
    }

    bool search(int key) const { return find(root_, key); }

    void remove(int key) {
        if (root_ == nullptr) return;
        erase(root_, key);
        // root may have become empty after a merge -> drop a level
        if (root_->keys.empty() && !root_->leaf) {
            Node* old = root_;
            root_ = root_->kids[0];
            old->kids.clear();
            delete old;
        } else if (root_->keys.empty() && root_->leaf) {
            delete root_;
            root_ = nullptr;
        }
    }

    // Print one line per level so the fanout and height are easy to see.
    void print_levels() const {
        if (!root_) { std::cout << "(empty)\n"; return; }
        std::vector<Node*> level = {root_};
        int depth = 0;
        while (!level.empty()) {
            std::cout << "L" << depth << ": ";
            std::vector<Node*> next;
            for (Node* n : level) {
                std::cout << "[";
                for (size_t i = 0; i < n->keys.size(); ++i)
                    std::cout << n->keys[i] << (i + 1 < n->keys.size() ? " " : "");
                std::cout << "] ";
                for (Node* c : n->kids) next.push_back(c);
            }
            std::cout << "\n";
            level = next;
            ++depth;
        }
    }

    // In-order traversal -> keys come out sorted.
    void print_sorted() const { inorder(root_); std::cout << "\n"; }

private:
    struct Node {
        std::vector<int>   keys;
        std::vector<Node*> kids;
        bool               leaf;
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
    };

    int   t_;
    Node* root_;

    bool full(Node* n) const { return static_cast<int>(n->keys.size()) == 2 * t_ - 1; }

    void destroy(Node* n) {
        if (!n) return;
        for (Node* c : n->kids) destroy(c);
        delete n;
    }

    bool find(Node* n, int key) const {
        if (!n) return false;
        size_t i = 0;
        while (i < n->keys.size() && key > n->keys[i]) ++i;
        if (i < n->keys.size() && key == n->keys[i]) return true;
        if (n->leaf) return false;
        return find(n->kids[i], key);
    }

    // Split the full child parent->kids[i] into two, pushing its middle key up
    // into the parent.
    void split_child(Node* parent, int i) {
        Node* y = parent->kids[i];
        Node* z = new Node(y->leaf);
        int mid = y->keys[t_ - 1];

        // z takes the upper half of y's keys
        z->keys.assign(y->keys.begin() + t_, y->keys.end());
        y->keys.resize(t_ - 1);
        if (!y->leaf) {
            z->kids.assign(y->kids.begin() + t_, y->kids.end());
            y->kids.resize(t_);
        }
        parent->kids.insert(parent->kids.begin() + i + 1, z);
        parent->keys.insert(parent->keys.begin() + i, mid);
    }

    void insert_nonfull(Node* n, int key) {
        int i = static_cast<int>(n->keys.size()) - 1;
        if (n->leaf) {
            n->keys.push_back(0);
            while (i >= 0 && key < n->keys[i]) { n->keys[i + 1] = n->keys[i]; --i; }
            n->keys[i + 1] = key;
            return;
        }
        while (i >= 0 && key < n->keys[i]) --i;
        ++i;
        if (full(n->kids[i])) {
            split_child(n, i);
            if (key > n->keys[i]) ++i;
        }
        insert_nonfull(n->kids[i], key);
    }

    // ---- deletion helpers (borrow from a sibling, or merge) ----

    int max_key(Node* n) const { while (!n->leaf) n = n->kids.back();  return n->keys.back(); }
    int min_key(Node* n) const { while (!n->leaf) n = n->kids.front(); return n->keys.front(); }

    void merge(Node* n, int i) {
        Node* left  = n->kids[i];
        Node* right = n->kids[i + 1];
        left->keys.push_back(n->keys[i]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        if (!left->leaf)
            left->kids.insert(left->kids.end(), right->kids.begin(), right->kids.end());
        n->keys.erase(n->keys.begin() + i);
        n->kids.erase(n->kids.begin() + i + 1);
        right->kids.clear();
        delete right;
    }

    // Make sure n->kids[i] has at least t keys before we descend into it.
    void ensure_min(Node* n, int i) {
        if (static_cast<int>(n->kids[i]->keys.size()) >= t_) return;
        // try to borrow from the left sibling
        if (i > 0 && static_cast<int>(n->kids[i - 1]->keys.size()) >= t_) {
            Node* child = n->kids[i];
            Node* sib   = n->kids[i - 1];
            child->keys.insert(child->keys.begin(), n->keys[i - 1]);
            n->keys[i - 1] = sib->keys.back();
            sib->keys.pop_back();
            if (!child->leaf) {
                child->kids.insert(child->kids.begin(), sib->kids.back());
                sib->kids.pop_back();
            }
        // or from the right sibling
        } else if (i < static_cast<int>(n->keys.size()) &&
                   static_cast<int>(n->kids[i + 1]->keys.size()) >= t_) {
            Node* child = n->kids[i];
            Node* sib   = n->kids[i + 1];
            child->keys.push_back(n->keys[i]);
            n->keys[i] = sib->keys.front();
            sib->keys.erase(sib->keys.begin());
            if (!child->leaf) {
                child->kids.push_back(sib->kids.front());
                sib->kids.erase(sib->kids.begin());
            }
        // no spare sibling -> merge
        } else {
            if (i < static_cast<int>(n->keys.size())) merge(n, i);
            else                                       merge(n, i - 1);
        }
    }

    void erase(Node* n, int key) {
        int i = 0;
        while (i < static_cast<int>(n->keys.size()) && key > n->keys[i]) ++i;

        if (i < static_cast<int>(n->keys.size()) && n->keys[i] == key) {
            if (n->leaf) {
                n->keys.erase(n->keys.begin() + i);
            } else if (static_cast<int>(n->kids[i]->keys.size()) >= t_) {
                int pred = max_key(n->kids[i]);     // replace with predecessor
                n->keys[i] = pred;
                erase(n->kids[i], pred);
            } else if (static_cast<int>(n->kids[i + 1]->keys.size()) >= t_) {
                int succ = min_key(n->kids[i + 1]); // replace with successor
                n->keys[i] = succ;
                erase(n->kids[i + 1], succ);
            } else {
                merge(n, i);
                erase(n->kids[i], key);
            }
            return;
        }
        if (n->leaf) return;                        // key not in tree

        bool last = (i == static_cast<int>(n->keys.size()));
        ensure_min(n, i);
        // after a merge the child index may shift
        if (last && i > static_cast<int>(n->keys.size())) erase(n->kids[i - 1], key);
        else                                              erase(n->kids[i], key);
    }

    void inorder(Node* n) const {
        if (!n) return;
        for (size_t i = 0; i < n->keys.size(); ++i) {
            if (!n->leaf) inorder(n->kids[i]);
            std::cout << n->keys[i] << " ";
        }
        if (!n->leaf) inorder(n->kids.back());
    }
};

int main() {
    std::cout << "B-Tree (minimum degree t=2) | Akshat Kushwaha | 24BCS10060\n\n";
    BTree tree(2);   // t=2 -> a 2-3-4 tree, easy to watch split

    const std::vector<int> keys = {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25, 40, 50, 22};
    std::cout << "inserting: ";
    for (int k : keys) { std::cout << k << " "; tree.insert(k); }
    std::cout << "\n\n";

    std::cout << "tree by level (shows fanout and height):\n";
    tree.print_levels();

    std::cout << "\nin-order (sorted): ";
    tree.print_sorted();

    std::cout << "\nsearch 17 = " << (tree.search(17) ? "found" : "not found") << "\n";
    std::cout << "search 99 = " << (tree.search(99) ? "found" : "not found") << "\n";

    std::cout << "\nremoving 6 and 20...\n";
    tree.remove(6);
    tree.remove(20);
    tree.print_levels();
    std::cout << "in-order after removals: ";
    tree.print_sorted();
    return 0;
}
