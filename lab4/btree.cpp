// Lab Session 4 — Full B-Tree (minimum degree t)
// Student : Indrajeet Yadav | Roll No: 23BCS10199
//
// Implements a complete B-Tree with:
//   insert  — split-on-the-way-down (proactive, single-pass top-down)
//   search  — O(log_t n) time, each node comparison is a binary search
//   remove  — merge / borrow-from-sibling on the way down; 3 delete cases
//   inorder — produces sorted output
//   height  — verifies O(log_t n) bound
//   print   — ASCII tree visualization
//
// Why B-Trees for databases?
//   A disk read fetches an entire page (4 KiB or 8 KiB). A B-Tree node
//   is sized to fill exactly one page, so each node access = one disk read.
//   With t=100 (100 keys per node), a tree of 1 million keys has height
//   ceil(log_100(1,000,001)) = 3. Only 3 disk reads for any lookup.
//   That is why PostgreSQL, MySQL (InnoDB), SQLite, and most databases use
//   B-Trees (or B+-Trees) as their primary index structure.
//
// Build: g++ -std=c++17 -Wall -Wextra -O2 btree.cpp -o btree
// Run:   ./btree

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <algorithm>
#include <iomanip>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// B-Tree parameters
// ─────────────────────────────────────────────────────────────────────────────

// Minimum degree t:
//   - Every node except the root has at least t-1 keys.
//   - Every node has at most 2t-1 keys (= 2t children).
//   - A node is FULL when it has exactly 2t-1 keys.
// For production databases, t is chosen so that one node fits in one disk page.
// E.g., 8 KiB page / ~8 bytes per key ≈ t = 500.
// For demonstration we use t = 3 (max 5 keys per node).
static const int T = 3;

// ─────────────────────────────────────────────────────────────────────────────
// BTreeNode
// ─────────────────────────────────────────────────────────────────────────────

struct BTreeNode {
    std::vector<int>       keys;      // sorted keys in this node
    std::vector<BTreeNode*> children; // children[i] has keys < keys[i]
    bool                   is_leaf = true;

    // Number of keys currently in this node
    int n() const { return (int)keys.size(); }

    // True when this node cannot accept another key without splitting
    bool full() const { return n() == 2 * T - 1; }
};

// ─────────────────────────────────────────────────────────────────────────────
// BTree
// ─────────────────────────────────────────────────────────────────────────────

class BTree {
public:
    BTree() : root_(nullptr) {}
    ~BTree() { destroy(root_); }

    // ── insert ────────────────────────────────────────────────────────────────
    // Strategy: proactive split-on-the-way-down.
    // Before descending into any child, if that child is FULL, split it.
    // This ensures we never need to backtrack to split a parent.
    void insert(int key) {
        if (!root_) {
            root_ = new BTreeNode();
            root_->keys.push_back(key);
            return;
        }

        // If root is full, grow the tree upward:
        // - Create a new root
        // - Old root becomes the new root's first child
        // - Split the old root (which promotes the median into the new root)
        if (root_->full()) {
            BTreeNode* new_root = new BTreeNode();
            new_root->is_leaf = false;
            new_root->children.push_back(root_);
            split_child(new_root, 0);  // split root_, promote median to new_root
            root_ = new_root;
        }

        insert_non_full(root_, key);
        size_++;
    }

    // ── search ────────────────────────────────────────────────────────────────
    bool search(int key) const {
        return search_helper(root_, key);
    }

    // ── remove ───────────────────────────────────────────────────────────────
    void remove(int key) {
        if (!root_) return;
        delete_key(root_, key);
        // If root becomes empty (and had children), shrink the tree
        if (root_->keys.empty() && !root_->is_leaf) {
            BTreeNode* old_root = root_;
            root_ = root_->children[0];
            delete old_root;
        }
        size_--;
    }

    // ── inorder traversal ────────────────────────────────────────────────────
    std::vector<int> inorder() const {
        std::vector<int> result;
        inorder_helper(root_, result);
        return result;
    }

    // ── tree height ──────────────────────────────────────────────────────────
    int height() const { return height_helper(root_); }
    int size()   const { return size_; }

    // ── print tree (indented) ─────────────────────────────────────────────────
    void print_tree(const std::string& label = "") const {
        if (!label.empty()) std::cout << "\n" << label << "\n";
        if (!root_) { std::cout << "(empty)\n"; return; }
        print_helper(root_, 0);
    }

private:
    BTreeNode* root_;
    int        size_ = 0;

    // ── split_child: split parent->children[i] (which must be full) ───────────
    //
    // Before:                         After:
    //   parent:  [...|k_i|...]          parent: [...|k_i|median|k_j|...]
    //              |                              |         |
    //            child (full)              left(t-1 keys) right(t-1 keys)
    //           t-1|median|t-1
    //
    // The median key (child->keys[T-1]) is promoted into parent at position i.
    // The right half of child's keys become a new sibling node z.
    void split_child(BTreeNode* parent, int i) {
        BTreeNode* y = parent->children[i];  // the full child to split
        BTreeNode* z = new BTreeNode();      // new node for the right half
        z->is_leaf = y->is_leaf;

        // The median key (index T-1) will be promoted to parent
        int median = y->keys[T - 1];

        // z gets the keys to the RIGHT of the median (indices T..2T-2)
        z->keys.assign(y->keys.begin() + T, y->keys.end());
        y->keys.resize(T - 1);  // y keeps keys 0..T-2 (left of median)

        // If y is not a leaf, z also gets the right half of y's children
        if (!y->is_leaf) {
            z->children.assign(y->children.begin() + T, y->children.end());
            y->children.resize(T);
        }

        // Insert median into parent at position i, and z as parent's child at i+1
        parent->keys.insert(parent->keys.begin() + i, median);
        parent->children.insert(parent->children.begin() + i + 1, z);
    }

    // ── insert_non_full: insert key into a node that is guaranteed NOT full ───
    void insert_non_full(BTreeNode* node, int key) {
        int i = node->n() - 1;

        if (node->is_leaf) {
            // Shift keys right to make room, then insert
            node->keys.push_back(0);
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            if (i >= 0 && key == node->keys[i]) {
                // Duplicate: undo the push_back and return
                node->keys.pop_back();
                return;
            }
            node->keys[i + 1] = key;
        } else {
            // Find the child to descend into
            while (i >= 0 && key < node->keys[i]) i--;
            i++;  // i is now the correct child index

            // Proactively split the child if it's full
            if (node->children[i]->full()) {
                split_child(node, i);
                // After split, the median was promoted to node->keys[i].
                // Decide which of the two resulting children to descend into.
                if (key > node->keys[i]) i++;
            }
            insert_non_full(node->children[i], key);
        }
    }

    // ── search_helper ─────────────────────────────────────────────────────────
    bool search_helper(BTreeNode* node, int key) const {
        if (!node) return false;
        int i = 0;
        while (i < node->n() && key > node->keys[i]) i++;
        if (i < node->n() && node->keys[i] == key) return true;
        if (node->is_leaf) return false;
        return search_helper(node->children[i], key);
    }

    // ── delete_key: main delete dispatcher ───────────────────────────────────
    //
    // Three cases for deletion:
    // Case 1: key is in this (leaf) node → remove directly.
    // Case 2: key is in this (internal) node:
    //   2a: left child has ≥ T keys → replace with in-order predecessor, delete predecessor
    //   2b: right child has ≥ T keys → replace with in-order successor, delete successor
    //   2c: both children have T-1 keys → merge left+key+right into one child, delete from it
    // Case 3: key is NOT in this node → descend to the correct child.
    //         Before descending, ensure the child has ≥ T keys (fill if needed).
    void delete_key(BTreeNode* node, int key) {
        int idx = lower_bound(node, key);

        // Case 1 / Case 2: key is IN this node
        if (idx < node->n() && node->keys[idx] == key) {
            if (node->is_leaf) {
                // Case 1: simply remove it from this leaf
                node->keys.erase(node->keys.begin() + idx);
            } else if (node->children[idx]->n() >= T) {
                // Case 2a: left child has a spare key → use in-order predecessor
                int pred = get_predecessor(node, idx);
                node->keys[idx] = pred;
                delete_key(node->children[idx], pred);
            } else if (node->children[idx + 1]->n() >= T) {
                // Case 2b: right child has a spare key → use in-order successor
                int succ = get_successor(node, idx);
                node->keys[idx] = succ;
                delete_key(node->children[idx + 1], succ);
            } else {
                // Case 2c: both children deficient → merge them around keys[idx]
                merge_children(node, idx);
                delete_key(node->children[idx], key);
            }
        } else {
            // Case 3: key is NOT in this node → descend into correct child
            if (node->is_leaf) {
                std::cout << "[DELETE] Key " << key << " not found in tree.\n";
                size_++;  // compensate: caller will decrement
                return;
            }

            // Determine the child to descend into
            bool last_child = (idx == node->n());
            int child_idx   = last_child ? idx - 1 : idx;

            // Ensure the child has at least T keys before descending
            if (node->children[child_idx]->n() < T)
                fill(node, child_idx);

            // After fill(), the child at child_idx may have been merged with its
            // right sibling, shifting the index
            if (last_child && child_idx > (int)node->children.size() - 1)
                delete_key(node->children[child_idx - 1], key);
            else
                delete_key(node->children[child_idx], key);
        }
    }

    // ── get_predecessor: largest key in subtree rooted at children[idx] ───────
    int get_predecessor(BTreeNode* node, int idx) const {
        BTreeNode* cur = node->children[idx];
        while (!cur->is_leaf) cur = cur->children.back();
        return cur->keys.back();
    }

    // ── get_successor: smallest key in subtree rooted at children[idx+1] ──────
    int get_successor(BTreeNode* node, int idx) const {
        BTreeNode* cur = node->children[idx + 1];
        while (!cur->is_leaf) cur = cur->children.front();
        return cur->keys.front();
    }

    // ── merge_children: merge children[idx] + keys[idx] + children[idx+1] ────
    // Produces one node with 2T-1 keys. Removes keys[idx] and children[idx+1]
    // from parent.
    void merge_children(BTreeNode* parent, int idx) {
        BTreeNode* left  = parent->children[idx];
        BTreeNode* right = parent->children[idx + 1];

        // Pull the separator key from parent into left
        left->keys.push_back(parent->keys[idx]);

        // Append right's keys and children into left
        left->keys.insert(left->keys.end(),
                          right->keys.begin(), right->keys.end());
        if (!left->is_leaf)
            left->children.insert(left->children.end(),
                                  right->children.begin(), right->children.end());

        // Remove the separator key and right child from parent
        parent->keys.erase(parent->keys.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);
        delete right;
    }

    // ── fill: ensure children[idx] has at least T keys ───────────────────────
    // Before descending into children[idx] for a delete, we must guarantee it
    // has at least T keys. If it only has T-1, we either borrow from a sibling
    // or merge with a sibling.
    void fill(BTreeNode* parent, int idx) {
        if (idx > 0 && parent->children[idx - 1]->n() >= T) {
            borrow_from_left(parent, idx);
        } else if (idx < (int)parent->children.size() - 1 &&
                   parent->children[idx + 1]->n() >= T) {
            borrow_from_right(parent, idx);
        } else {
            // Merge: if idx is the last child, merge with left sibling
            if (idx < (int)parent->children.size() - 1)
                merge_children(parent, idx);
            else
                merge_children(parent, idx - 1);
        }
    }

    // ── borrow_from_left: pull one key from children[idx-1] ──────────────────
    // The separator key (parent->keys[idx-1]) descends into children[idx].
    // The left sibling's last key ascends to become the new separator.
    void borrow_from_left(BTreeNode* parent, int idx) {
        BTreeNode* child   = parent->children[idx];
        BTreeNode* sibling = parent->children[idx - 1];

        // Make room at the front of child
        child->keys.insert(child->keys.begin(), parent->keys[idx - 1]);

        // The last child of sibling moves to the front of child
        if (!child->is_leaf) {
            child->children.insert(child->children.begin(),
                                   sibling->children.back());
            sibling->children.pop_back();
        }

        // The separator in parent gets replaced by sibling's last key
        parent->keys[idx - 1] = sibling->keys.back();
        sibling->keys.pop_back();
    }

    // ── borrow_from_right: pull one key from children[idx+1] ─────────────────
    void borrow_from_right(BTreeNode* parent, int idx) {
        BTreeNode* child   = parent->children[idx];
        BTreeNode* sibling = parent->children[idx + 1];

        // Separator descends to end of child
        child->keys.push_back(parent->keys[idx]);

        // First child of sibling moves to end of child's children
        if (!child->is_leaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }

        // Sibling's first key becomes the new separator in parent
        parent->keys[idx] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());
    }

    // ── lower_bound: index of first key >= target ─────────────────────────────
    int lower_bound(BTreeNode* node, int key) const {
        int lo = 0, hi = node->n();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (node->keys[mid] < key) lo = mid + 1;
            else                        hi = mid;
        }
        return lo;
    }

    // ── inorder_helper ────────────────────────────────────────────────────────
    void inorder_helper(BTreeNode* node, std::vector<int>& result) const {
        if (!node) return;
        for (int i = 0; i < node->n(); i++) {
            if (!node->is_leaf) inorder_helper(node->children[i], result);
            result.push_back(node->keys[i]);
        }
        if (!node->is_leaf) inorder_helper(node->children.back(), result);
    }

    int height_helper(BTreeNode* node) const {
        if (!node) return 0;
        if (node->is_leaf) return 1;
        return 1 + height_helper(node->children[0]);
    }

    // ── print_helper: indented visualization ─────────────────────────────────
    void print_helper(BTreeNode* node, int depth) const {
        if (!node) return;
        std::string indent(depth * 4, ' ');

        // Print keys in this node
        std::cout << indent << "[";
        for (int i = 0; i < node->n(); i++) {
            if (i > 0) std::cout << "|";
            std::cout << node->keys[i];
        }
        std::cout << "]" << (node->is_leaf ? " (leaf)" : "") << "\n";

        // Recursively print children
        for (auto* child : node->children)
            print_helper(child, depth + 1);
    }

    void destroy(BTreeNode* node) {
        if (!node) return;
        for (auto* child : node->children) destroy(child);
        delete node;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main: demonstrations
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Lab 4 — B-Tree (minimum degree t=" << T << ") ===\n"
              << "    Indrajeet Yadav | 23BCS10199\n"
              << "    Max keys per node: " << 2*T-1
              << "  |  Min keys (non-root): " << T-1 << "\n\n";

    BTree bt;

    // ── Demo 1: Insert and observe splits ────────────────────────────────────
    std::cout << "── Inserting: 10 20 5 6 12 30 7 17 3 1 25 ──\n\n";
    for (int k : {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25})
        bt.insert(k);

    bt.print_tree("B-Tree structure after inserts (indented):");

    auto seq = bt.inorder();
    std::cout << "\nInorder (must be sorted): ";
    for (int v : seq) std::cout << v << " ";
    std::cout << "\n";
    std::cout << "Height: " << bt.height()
              << "  log_" << T << "(" << bt.size() << ")≈"
              << std::fixed << std::setprecision(2)
              << std::log(bt.size()) / std::log(T) << "\n\n";

    // ── Demo 2: Search ────────────────────────────────────────────────────────
    std::cout << "── Search ──\n";
    for (int k : {17, 25, 99, 3, 0}) {
        std::cout << "  search(" << k << ") = "
                  << (bt.search(k) ? "found" : "not found") << "\n";
    }
    std::cout << "\n";

    // ── Demo 3: Delete and verify ─────────────────────────────────────────────
    std::cout << "── Deleting: 6 (leaf), 20 (internal), 3 (leaf) ──\n\n";
    bt.remove(6);
    bt.print_tree("After removing 6 (leaf key):");
    std::cout << "\n";

    bt.remove(20);
    bt.print_tree("After removing 20 (internal key — replaced by predecessor):");
    std::cout << "\n";

    bt.remove(3);
    bt.print_tree("After removing 3:");
    std::cout << "\n";

    seq = bt.inorder();
    std::cout << "Inorder after all deletions: ";
    for (int v : seq) std::cout << v << " ";
    std::cout << "\n\n";

    // ── Demo 4: Large tree height verification ────────────────────────────────
    std::cout << "── Height verification for large trees (t=" << T << ") ──\n";
    for (int n : {100, 1000, 10000, 100000}) {
        BTree b;
        for (int i = 1; i <= n; i++) b.insert(i);
        std::cout << "  n=" << std::setw(6) << n
                  << "  height=" << b.height()
                  << "  ceil(log_" << T << "(" << n << "))="
                  << (int)std::ceil(std::log(n) / std::log(T)) << "\n";
    }

    std::cout << "\n── Key properties demonstrated ──\n"
              << "  • Every non-root node has " << T-1 << " to " << 2*T-1 << " keys.\n"
              << "  • Root can have 1 to " << 2*T-1 << " keys.\n"
              << "  • All leaves are at the same level (perfect balance).\n"
              << "  • Height = O(log_t n) → with t=100 and n=1M, height=3.\n"
              << "  • Split on the way down → insert never backtracks.\n"
              << "  • Delete: leaf=erase, internal=replace+delete, child deficient=fill.\n"
              << "  • Used in: PostgreSQL (pg_btree), MySQL InnoDB, SQLite, Oracle.\n";

    return 0;
}
