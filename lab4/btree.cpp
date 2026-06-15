// Lab 4, Part 2: B-Tree (minimum degree t)
// Aditya Bhaskara (24BCS10058)
//
// The B-Tree is the index structure that actually lives on disk in PostgreSQL,
// MySQL/InnoDB and SQLite. Each node is sized to a disk page and holds many
// keys, so one page read pulls in a whole node and the tree stays shallow. That
// is the whole point: fewer levels means fewer disk seeks.
//
// With minimum degree t:
//   - every node other than the root holds between t-1 and 2t-1 keys,
//   - every internal node has one more child than it has keys,
//   - the root may hold as few as 1 key.
//
// We insert top down, splitting any full child before we descend into it, and
// delete top down, making sure each child we step into has at least t keys
// (borrowing from a sibling or merging when it does not). Both passes keep the
// tree balanced without ever walking back up.
//
// Build: g++ -std=c++17 -o btree btree.cpp
// Run:   ./btree

#include <iostream>
#include <queue>
#include <vector>

// Minimum degree. t = 2 makes a 2-3-4 tree, which splits often and so makes the
// structure easy to watch in a small demo. Bump it up for a wider, flatter tree.
constexpr int kMinDegree = 2;

struct BNode {
    std::vector<int>    keys;
    std::vector<BNode*> children;
    bool                leaf;

    explicit BNode(bool is_leaf) : leaf(is_leaf) {}

    bool full() const { return static_cast<int>(keys.size()) == 2 * kMinDegree - 1; }
};

class BTree {
public:
    BTree() : root_(new BNode(true)) {}
    ~BTree() { destroy(root_); }

    void insert(int key) {
        if (root_->full()) {
            // Grow upward: a fresh root adopts the old one, then we split it.
            BNode* new_root = new BNode(false);
            new_root->children.push_back(root_);
            split_child(new_root, 0);
            root_ = new_root;
        }
        insert_nonfull(root_, key);
    }

    void remove(int key) {
        if (!search(key)) {
            std::cout << "  (key " << key << " not present)\n";
            return;
        }
        erase(root_, key);
        // The root may end up empty after a merge; drop the now-redundant level.
        if (root_->keys.empty() && !root_->leaf) {
            BNode* old = root_;
            root_ = root_->children[0];
            delete old;
        }
    }

    bool search(int key) const { return search(root_, key); }

    void print_inorder() const {
        inorder(root_);
        std::cout << "\n";
    }

    // Prints the tree one level at a time so node boundaries are visible, e.g.
    // [17]  /  [5 10] [25 30]  shows the root key and its two children.
    void print_structure() const {
        std::queue<BNode*> level;
        level.push(root_);
        while (!level.empty()) {
            int count = static_cast<int>(level.size());
            for (int i = 0; i < count; ++i) {
                BNode* node = level.front();
                level.pop();
                print_node(node);
                std::cout << (i + 1 < count ? "  " : "");
                for (BNode* child : node->children) level.push(child);
            }
            std::cout << "\n";
        }
    }

private:
    void destroy(BNode* node) {
        if (!node) return;
        for (BNode* child : node->children) destroy(child);
        delete node;
    }

    // Split children[i], which must be full. Its median key moves up into the
    // parent and the upper half of its keys move into a new right sibling.
    void split_child(BNode* parent, int i) {
        BNode* full = parent->children[i];
        BNode* sibling = new BNode(full->leaf);

        sibling->keys.assign(full->keys.begin() + kMinDegree, full->keys.end());
        if (!full->leaf)
            sibling->children.assign(full->children.begin() + kMinDegree,
                                     full->children.end());

        int median = full->keys[kMinDegree - 1];
        full->keys.resize(kMinDegree - 1);
        if (!full->leaf) full->children.resize(kMinDegree);

        parent->children.insert(parent->children.begin() + i + 1, sibling);
        parent->keys.insert(parent->keys.begin() + i, median);
    }

    void insert_nonfull(BNode* node, int key) {
        int i = static_cast<int>(node->keys.size()) - 1;
        if (node->leaf) {
            node->keys.push_back(0);
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                --i;
            }
            node->keys[i + 1] = key;
            return;
        }

        while (i >= 0 && key < node->keys[i]) --i;
        ++i;
        if (node->children[i]->full()) {
            split_child(node, i);
            if (key > node->keys[i]) ++i;   // median rose up, pick the right side
        }
        insert_nonfull(node->children[i], key);
    }

    bool search(BNode* node, int key) const {
        int i = 0;
        while (i < static_cast<int>(node->keys.size()) && key > node->keys[i]) ++i;
        if (i < static_cast<int>(node->keys.size()) && node->keys[i] == key) return true;
        return node->leaf ? false : search(node->children[i], key);
    }

    int predecessor(BNode* node, int idx) const {
        BNode* cur = node->children[idx];
        while (!cur->leaf) cur = cur->children.back();
        return cur->keys.back();
    }

    int successor(BNode* node, int idx) const {
        BNode* cur = node->children[idx + 1];
        while (!cur->leaf) cur = cur->children.front();
        return cur->keys.front();
    }

    // Merge children[idx] and children[idx+1] together with the separating key
    // keys[idx] pulled down between them.
    void merge(BNode* node, int idx) {
        BNode* left  = node->children[idx];
        BNode* right = node->children[idx + 1];

        left->keys.push_back(node->keys[idx]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        if (!left->leaf)
            left->children.insert(left->children.end(),
                                  right->children.begin(), right->children.end());

        node->keys.erase(node->keys.begin() + idx);
        node->children.erase(node->children.begin() + idx + 1);
        delete right;
    }

    // Make sure children[idx] has at least t keys before we descend into it,
    // either by borrowing one key from a sibling or by merging with one.
    void ensure_min_keys(BNode* node, int idx) {
        const int t = kMinDegree;
        if (idx > 0 && static_cast<int>(node->children[idx - 1]->keys.size()) >= t) {
            borrow_from_prev(node, idx);
        } else if (idx < static_cast<int>(node->children.size()) - 1 &&
                   static_cast<int>(node->children[idx + 1]->keys.size()) >= t) {
            borrow_from_next(node, idx);
        } else if (idx < static_cast<int>(node->children.size()) - 1) {
            merge(node, idx);
        } else {
            merge(node, idx - 1);
        }
    }

    void borrow_from_prev(BNode* node, int idx) {
        BNode* child   = node->children[idx];
        BNode* sibling = node->children[idx - 1];
        // Separator drops into child, sibling's last key rises to be separator.
        child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
        node->keys[idx - 1] = sibling->keys.back();
        sibling->keys.pop_back();
        if (!child->leaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }
    }

    void borrow_from_next(BNode* node, int idx) {
        BNode* child   = node->children[idx];
        BNode* sibling = node->children[idx + 1];
        child->keys.push_back(node->keys[idx]);
        node->keys[idx] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());
        if (!child->leaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }
    }

    void erase(BNode* node, int key) {
        int idx = 0;
        while (idx < static_cast<int>(node->keys.size()) && key > node->keys[idx]) ++idx;

        bool here = idx < static_cast<int>(node->keys.size()) && node->keys[idx] == key;

        if (here && node->leaf) {
            node->keys.erase(node->keys.begin() + idx);
            return;
        }

        if (here) {
            // Internal node. Replace the key with a neighbour we can safely
            // delete from a fat child, or merge the two thin children and recurse.
            if (static_cast<int>(node->children[idx]->keys.size()) >= kMinDegree) {
                int pred = predecessor(node, idx);
                node->keys[idx] = pred;
                erase(node->children[idx], pred);
            } else if (static_cast<int>(node->children[idx + 1]->keys.size()) >= kMinDegree) {
                int succ = successor(node, idx);
                node->keys[idx] = succ;
                erase(node->children[idx + 1], succ);
            } else {
                merge(node, idx);
                erase(node->children[idx], key);
            }
            return;
        }

        if (node->leaf) return;   // key is genuinely absent on this path

        // Key is somewhere below. Top down rule: fatten the child first.
        bool last_child = idx == static_cast<int>(node->keys.size());
        if (static_cast<int>(node->children[idx]->keys.size()) < kMinDegree)
            ensure_min_keys(node, idx);

        // A merge may have shifted the target one slot to the left.
        if (last_child && idx > static_cast<int>(node->keys.size()))
            erase(node->children[idx - 1], key);
        else
            erase(node->children[idx], key);
    }

    void inorder(BNode* node) const {
        for (size_t i = 0; i < node->keys.size(); ++i) {
            if (!node->leaf) inorder(node->children[i]);
            std::cout << node->keys[i] << " ";
        }
        if (!node->leaf) inorder(node->children.back());
    }

    void print_node(BNode* node) const {
        std::cout << "[";
        for (size_t i = 0; i < node->keys.size(); ++i)
            std::cout << node->keys[i] << (i + 1 < node->keys.size() ? " " : "");
        std::cout << "]";
    }

    BNode* root_;
};

int main() {
    BTree tree;

    const std::vector<int> keys = {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25, 40, 50, 60};
    std::cout << "inserting:";
    for (int k : keys) std::cout << " " << k;
    std::cout << "  (minimum degree t = " << kMinDegree << ")\n\n";
    for (int k : keys) tree.insert(k);

    std::cout << "inorder traversal: ";
    tree.print_inorder();
    std::cout << "\ntree by level:\n";
    tree.print_structure();

    std::cout << "search 17: " << (tree.search(17) ? "found" : "not found") << "\n";
    std::cout << "search 99: " << (tree.search(99) ? "found" : "not found") << "\n\n";

    for (int k : {6, 20, 1, 50}) {
        std::cout << "removing " << k << ":\n";
        tree.remove(k);
        tree.print_structure();
        std::cout << "inorder: ";
        tree.print_inorder();
        std::cout << "\n";
    }
    return 0;
}
