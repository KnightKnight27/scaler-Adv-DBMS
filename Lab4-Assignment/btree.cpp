// Lab 4 — Part 2: Full B-Tree of minimum degree T.
// An internal node holds [T-1 .. 2T-1] keys and [T .. 2T] children; the root
// may hold as few as 1 key. Supports insert (split-on-the-way-down), search,
// and delete (borrow/merge-on-the-way-down).
//
// NOTE: the lab handout's split_child() read y->keys[T-1] and
// y->keys.begin()+T *after* shrinking y (resize to T-1) — that is an
// out-of-bounds read (undefined behaviour). Fixed here by capturing the median
// and the right-half slices BEFORE resizing y. See README for details.
//
// Build: g++ -std=c++17 -O2 -o btree btree.cpp
#include <iostream>
#include <vector>

const int T = 2;   // minimum degree (fanout). Higher T => shorter tree.

struct BNode {
    std::vector<int>    keys;
    std::vector<BNode*> children;
    bool                leaf = true;
};

class BTree {
    BNode* root = nullptr;

    // Split a FULL child (2T-1 keys) of `parent` at index i: the right half and
    // the median are extracted from y FIRST, then y is shrunk, then the median
    // is promoted into the parent and the new right node z is linked in.
    void split_child(BNode* parent, int i) {
        BNode* y = parent->children[i];
        BNode* z = new BNode();
        z->leaf = y->leaf;

        int med = y->keys[T - 1];                                   // (1) capture median first
        z->keys.assign(y->keys.begin() + T, y->keys.end());        // (2) z = right half of keys
        if (!y->leaf)
            z->children.assign(y->children.begin() + T, y->children.end());

        y->keys.resize(T - 1);                                      // (3) now shrink y
        if (!y->leaf) y->children.resize(T);

        parent->keys.insert(parent->keys.begin() + i, med);        // (4) promote median
        parent->children.insert(parent->children.begin() + i + 1, z);
    }

    void insert_non_full(BNode* node, int key) {
        int i = (int)node->keys.size() - 1;
        if (node->leaf) {
            node->keys.push_back(0);
            while (i >= 0 && key < node->keys[i]) { node->keys[i + 1] = node->keys[i]; i--; }
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

    int get_predecessor(BNode* node, int idx) {
        BNode* cur = node->children[idx];
        while (!cur->leaf) cur = cur->children.back();
        return cur->keys.back();
    }
    int get_successor(BNode* node, int idx) {
        BNode* cur = node->children[idx + 1];
        while (!cur->leaf) cur = cur->children.front();
        return cur->keys.front();
    }

    // Merge children[idx] and children[idx+1] around the separator keys[idx].
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

    // Ensure children[idx] has >= T keys before we descend into it.
    void fill(BNode* node, int idx) {
        if (idx > 0 && (int)node->children[idx - 1]->keys.size() >= T) {
            BNode* child   = node->children[idx];
            BNode* sibling = node->children[idx - 1];
            child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
            node->keys[idx - 1] = sibling->keys.back();
            sibling->keys.pop_back();
            if (!child->leaf) {
                child->children.insert(child->children.begin(), sibling->children.back());
                sibling->children.pop_back();
            }
        } else if (idx < (int)node->children.size() - 1 &&
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
        } else {
            if (idx < (int)node->children.size() - 1) merge(node, idx);
            else                                      merge(node, idx - 1);
        }
    }

    void delete_key(BNode* node, int key) {
        int idx = 0;
        while (idx < (int)node->keys.size() && key > node->keys[idx]) idx++;

        if (idx < (int)node->keys.size() && node->keys[idx] == key) {
            if (node->leaf) {
                node->keys.erase(node->keys.begin() + idx);
            } else if ((int)node->children[idx]->keys.size() >= T) {
                int pred = get_predecessor(node, idx);
                node->keys[idx] = pred;
                delete_key(node->children[idx], pred);
            } else if ((int)node->children[idx + 1]->keys.size() >= T) {
                int succ = get_successor(node, idx);
                node->keys[idx] = succ;
                delete_key(node->children[idx + 1], succ);
            } else {
                merge(node, idx);
                delete_key(node->children[idx], key);
            }
        } else {
            if (node->leaf) { std::cout << "  (key " << key << " not found)\n"; return; }
            bool last = (idx == (int)node->children.size() - 1);
            if ((int)node->children[idx]->keys.size() < T) fill(node, idx);
            // after fill, the target may have shifted; clamp index
            if (idx > (int)node->keys.size()) delete_key(node->children[idx - 1], key);
            else                              delete_key(node->children[idx], key);
        }
    }

    void inorder(BNode* node) const {
        if (!node) return;
        for (int i = 0; i < (int)node->keys.size(); i++) {
            if (!node->leaf) inorder(node->children[i]);
            std::cout << node->keys[i] << " ";
        }
        if (!node->leaf) inorder(node->children.back());
    }

    // Pretty-print the tree level by level to show node packing / height.
    void show_structure(BNode* node, int depth) const {
        if (!node) return;
        std::cout << std::string(depth * 4, ' ') << "[";
        for (size_t i = 0; i < node->keys.size(); i++)
            std::cout << node->keys[i] << (i + 1 < node->keys.size() ? " " : "");
        std::cout << "]" << (node->leaf ? " (leaf)" : "") << "\n";
        for (auto* c : node->children) show_structure(c, depth + 1);
    }

public:
    void insert(int key) {
        if (!root) { root = new BNode(); root->keys.push_back(key); return; }
        if ((int)root->keys.size() == 2 * T - 1) {     // root full: grow height
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
        if (root->keys.empty() && !root->leaf) {       // root emptied: shrink height
            BNode* old = root;
            root = root->children[0];
            delete old;
        }
    }

    bool search(BNode* node, int key) const {
        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i]) i++;
        if (i < (int)node->keys.size() && node->keys[i] == key) return true;
        if (node->leaf) return false;
        return search(node->children[i], key);
    }
    bool search(int key) const { return root && search(root, key); }

    void print() const { inorder(root); std::cout << "\n"; }
    void structure() const { std::cout << "Tree structure (T=" << T << "):\n"; show_structure(root, 0); }
};

int main() {
    BTree bt;
    std::cout << "Insert sequence: 10 20 5 6 12 30 7 17 3 1 25\n";
    for (int k : {10, 20, 5, 6, 12, 30, 7, 17, 3, 1, 25}) bt.insert(k);

    std::cout << "\nInorder after inserts (must be sorted): ";
    bt.print();
    bt.structure();

    std::cout << "\nSearch 17: " << (bt.search(17) ? "found" : "not found") << "\n";
    std::cout << "Search 99: " << (bt.search(99) ? "found" : "not found") << "\n";

    std::cout << "\nRemove 6 and 20:\n";
    bt.remove(6);
    bt.remove(20);
    std::cout << "Inorder after deletes: ";
    bt.print();
    bt.structure();
    return 0;
}
