// Lab 4, Part 2: Full B-Tree (order t)
// Compile: g++ -std=c++17 -o btree btree.cpp
// Run:     ./btree
//
// Every node: t-1 .. 2t-1 keys, t .. 2t children (root can have fewer).
// T = minimum degree.  PostgreSQL B-Tree index pages are 8 KB by default
// (matching PRAGMA page_size from Lab 2).

#include <iostream>
#include <vector>
#include <algorithm>
#include <iomanip>

const int T = 3;  // minimum degree — controls fanout (higher = shorter tree)

struct BNode {
    std::vector<int>    keys;
    std::vector<BNode*> children;
    bool                leaf = true;

    ~BNode() {
        for (auto* c : children) delete c;
    }
};

class BTree {
    BNode* root = nullptr;

    // ── Split full child[i] of parent; promote median to parent ──
    void split_child(BNode* parent, int i) {
        BNode* y    = parent->children[i];
        BNode* z    = new BNode();
        z->leaf     = y->leaf;

        // Right half of keys → z
        z->keys.assign(y->keys.begin() + T, y->keys.end());
        int median  = y->keys[T - 1];
        y->keys.resize(T - 1);

        if (!y->leaf) {
            z->children.assign(y->children.begin() + T, y->children.end());
            y->children.resize(T);
        }

        parent->keys.insert(parent->keys.begin() + i, median);
        parent->children.insert(parent->children.begin() + i + 1, z);
    }

    // Insert into a node that is guaranteed not full
    void insert_non_full(BNode* node, int key) {
        int i = (int)node->keys.size() - 1;
        if (node->leaf) {
            node->keys.push_back(0);
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = key;
        } else {
            while (i >= 0 && key < node->keys[i]) i--;
            i++;
            if ((int)node->children[i]->keys.size() == 2 * T - 1) {
                split_child(node, i);
                if (key > node->keys[i]) i++;
            }
            insert_non_full(node->children[i], key);
        }
    }

    // Predecessor: largest key in left subtree of keys[idx]
    int get_pred(BNode* node, int idx) {
        BNode* cur = node->children[idx];
        while (!cur->leaf) cur = cur->children.back();
        return cur->keys.back();
    }

    // Successor: smallest key in right subtree of keys[idx]
    int get_succ(BNode* node, int idx) {
        BNode* cur = node->children[idx + 1];
        while (!cur->leaf) cur = cur->children.front();
        return cur->keys.front();
    }

    // Merge children[idx] and children[idx+1] around keys[idx]
    void merge(BNode* node, int idx) {
        BNode* left  = node->children[idx];
        BNode* right = node->children[idx + 1];

        left->keys.push_back(node->keys[idx]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        if (!left->leaf) {
            left->children.insert(left->children.end(),
                                  right->children.begin(), right->children.end());
            right->children.clear();   // ownership transferred; prevent double-delete
        }

        node->keys.erase(node->keys.begin() + idx);
        node->children.erase(node->children.begin() + idx + 1);
        delete right;
    }

    // Ensure child[idx] has at least T keys before descending.
    // Either borrow from a sibling or merge with a sibling.
    void fill(BNode* node, int idx) {
        // Try to borrow from left sibling
        if (idx != 0 &&
            (int)node->children[idx - 1]->keys.size() >= T) {
            BNode* child   = node->children[idx];
            BNode* sibling = node->children[idx - 1];
            child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
            node->keys[idx - 1] = sibling->keys.back();
            sibling->keys.pop_back();
            if (!child->leaf) {
                child->children.insert(child->children.begin(),
                                       sibling->children.back());
                sibling->children.pop_back();
            }
            return;
        }
        // Try to borrow from right sibling
        if (idx != (int)node->children.size() - 1 &&
            (int)node->children[idx + 1]->keys.size() >= T) {
            BNode* child   = node->children[idx];
            BNode* sibling = node->children[idx + 1];
            child->keys.push_back(node->keys[idx]);
            node->keys[idx] = sibling->keys.front();
            sibling->keys.erase(sibling->keys.begin());
            if (!child->leaf) {
                child->children.push_back(sibling->children.front());
                sibling->children.erase(sibling->children.begin());
            }
            return;
        }
        // Merge with a sibling
        if (idx != (int)node->children.size() - 1)
            merge(node, idx);
        else
            merge(node, idx - 1);
    }

    void delete_key(BNode* node, int key) {
        int idx = 0;
        while (idx < (int)node->keys.size() && key > node->keys[idx]) idx++;

        // Case 1: key is in this node
        if (idx < (int)node->keys.size() && node->keys[idx] == key) {
            if (node->leaf) {
                node->keys.erase(node->keys.begin() + idx);
                return;
            }
            // Internal node — three cases:
            if ((int)node->children[idx]->keys.size() >= T) {
                // Case 2a: predecessor has enough keys
                int pred = get_pred(node, idx);
                node->keys[idx] = pred;
                delete_key(node->children[idx], pred);
            } else if ((int)node->children[idx + 1]->keys.size() >= T) {
                // Case 2b: successor has enough keys
                int succ = get_succ(node, idx);
                node->keys[idx] = succ;
                delete_key(node->children[idx + 1], succ);
            } else {
                // Case 2c: merge, then recurse
                merge(node, idx);
                delete_key(node->children[idx], key);
            }
            return;
        }

        // Case 3: key not in this node
        if (node->leaf) {
            std::cout << "Key " << key << " not found\n";
            return;
        }

        // Ensure child[idx] has >= T keys before descending
        bool last = (idx == (int)node->children.size() - 1);
        if ((int)node->children[idx]->keys.size() < T)
            fill(node, idx);

        // After fill, figure out which child to recurse into
        if (last && idx > (int)node->keys.size())
            delete_key(node->children[idx - 1], key);
        else
            delete_key(node->children[idx], key);
    }

    bool search_node(BNode* node, int key) const {
        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i]) i++;
        if (i < (int)node->keys.size() && node->keys[i] == key) return true;
        if (node->leaf) return false;
        return search_node(node->children[i], key);
    }

    int height(BNode* node) const {
        if (!node) return 0;
        if (node->leaf) return 1;
        return 1 + height(node->children[0]);
    }

    void inorder(BNode* node) const {
        if (!node) return;
        for (int i = 0; i < (int)node->keys.size(); i++) {
            if (!node->leaf) inorder(node->children[i]);
            std::cout << node->keys[i] << " ";
        }
        if (!node->leaf) inorder(node->children.back());
    }

    void verify_node(BNode* node, int depth, int& leaves_at_depth) const {
        if (!node) return;
        if (node != root) {
            if (node->keys.size() < (size_t)(T - 1) ||
                node->keys.size() > (size_t)(2 * T - 1)) {
                std::cerr << "VERIFY FAIL: node at depth " << depth
                          << " has " << node->keys.size() << " keys (T=" << T << ")\n";
                return;
            }
        }
        if (!node->leaf) {
            if ((int)node->children.size() < (int)node->keys.size() + 1 ||
                (int)node->children.size() > (int)node->keys.size() + 1) {
                std::cerr << "VERIFY FAIL: child count mismatch at depth " << depth << "\n";
                return;
            }
            for (size_t i = 0; i < node->children.size(); i++)
                verify_node(node->children[i], depth + 1, leaves_at_depth);
        } else {
            if (leaves_at_depth == -1) leaves_at_depth = depth;
            else if (depth != leaves_at_depth) {
                std::cerr << "VERIFY FAIL: leaves at differing depths\n";
            }
        }
    }

public:
    void insert(int key) {
        if (!root) {
            root = new BNode();
            root->keys.push_back(key);
            return;
        }
        if ((int)root->keys.size() == 2 * T - 1) {
            BNode* s = new BNode();
            s->leaf = false;
            s->children.push_back(root);
            split_child(s, 0);
            root = s;
        }
        insert_non_full(root, key);
    }

    void remove(int key) {
        if (!root) return;
        delete_key(root, key);
        if (root->keys.empty() && !root->leaf) {
            BNode* old = root;
            root = root->children[0];
            delete old;
        }
    }

    bool search(int key) const { return root && search_node(root, key); }
    int tree_height() const { return height(root); }

    void print() const {
        std::cout << "Inorder: ";
        inorder(root);
        std::cout << "\n";
    }

    void verify() const {
        if (!root) return;
        int leaves = -1;
        verify_node(root, 0, leaves);
        std::cout << "B-Tree verified OK (height=" << tree_height()
                  << ", all leaves at depth=" << leaves << ")\n";
    }
};

int main() {
    BTree bt;

    // ── Basic insert sequence ──
    std::vector<int> insert_seq = {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25};
    std::cout << "=== Inserting: ";
    for (int k : insert_seq) std::cout << k << " ";
    std::cout << "===\n";
    for (int k : insert_seq) bt.insert(k);
    bt.print();
    bt.verify();

    // ── Search ──
    std::cout << "\nSearch 17: " << (bt.search(17) ? "found" : "not found") << "\n";
    std::cout << "Search 99: " << (bt.search(99) ? "found" : "not found") << "\n";

    // ── Delete ──
    std::cout << "\n=== Removing 6 and 20 ===\n";
    bt.remove(6);
    bt.remove(20);
    bt.print();
    bt.verify();

    // ── Larger test: 50 inserts, 15 deletes ──
    std::cout << "\n=== Test: insert 1..50, remove 10 keys ===\n";
    BTree bt2;
    for (int i = 1; i <= 50; i++) bt2.insert(i);
    std::cout << "Height after 50 inserts: " << bt2.tree_height() << "\n";
    for (int i : {3, 7, 12, 18, 25, 30, 33, 40, 44, 47, 50}) bt2.remove(i);
    std::cout << "After 11 deletes:\n";
    bt2.print();
    for (int k : {1, 5, 10, 15, 20, 25, 30, 50})
        std::cout << "  Search " << std::setw(3) << k << ": "
                  << (bt2.search(k) ? "found" : "not found") << "\n";
    bt2.verify();

    return 0;
}