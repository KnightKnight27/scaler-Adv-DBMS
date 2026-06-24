// B-Tree (CLRS) — insert / search / delete with an interactive menu.
// Built with: g++ -std=c++17 -Wall -Wextra -O2 -o btree main.cpp
#include <iostream>
#include <vector>
#include <queue>

struct Node {
    std::vector<int>   keys;
    std::vector<Node*> kids;
    bool               isLeaf;

    explicit Node(bool leaf) : isLeaf(leaf) {}
};

class BTree {
    Node* root = nullptr;
    int   t;                       // minimum degree (>= 2)

    // ---- helpers operating on a node + child index ----

    // Split the full child kids[idx] (it has 2t-1 keys) of a non-full parent.
    void splitChild(Node* parent, int idx) {
        Node* full = parent->kids[idx];
        Node* sib  = new Node(full->isLeaf);

        // sib takes the top t-1 keys; full keeps the bottom t-1; median moves up.
        int median = full->keys[t - 1];
        sib->keys.assign(full->keys.begin() + t, full->keys.end());
        full->keys.resize(t - 1);

        if (!full->isLeaf) {
            sib->kids.assign(full->kids.begin() + t, full->kids.end());
            full->kids.resize(t);
        }

        parent->keys.insert(parent->keys.begin() + idx, median);
        parent->kids.insert(parent->kids.begin() + idx + 1, sib);
    }

    // Insert into a node guaranteed not to be full.
    void insertNonFull(Node* node, int key) {
        int i = (int)node->keys.size() - 1;
        if (node->isLeaf) {
            node->keys.push_back(0);
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                --i;
            }
            node->keys[i + 1] = key;
        } else {
            while (i >= 0 && key < node->keys[i]) --i;
            ++i;
            if ((int)node->kids[i]->keys.size() == 2 * t - 1) {
                splitChild(node, i);
                if (key > node->keys[i]) ++i;
            }
            insertNonFull(node->kids[i], key);
        }
    }

    bool search(Node* node, int key) const {
        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i]) ++i;
        if (i < (int)node->keys.size() && node->keys[i] == key) return true;
        if (node->isLeaf) return false;
        return search(node->kids[i], key);
    }

    int getPred(Node* node, int idx) const {
        Node* cur = node->kids[idx];
        while (!cur->isLeaf) cur = cur->kids.back();
        return cur->keys.back();
    }

    int getSucc(Node* node, int idx) const {
        Node* cur = node->kids[idx + 1];
        while (!cur->isLeaf) cur = cur->kids.front();
        return cur->keys.front();
    }

    // Merge kids[idx] + separator key + kids[idx+1] into one node of 2t-1 keys.
    void merge(Node* node, int idx) {
        Node* child = node->kids[idx];
        Node* sib   = node->kids[idx + 1];

        child->keys.push_back(node->keys[idx]);
        child->keys.insert(child->keys.end(), sib->keys.begin(), sib->keys.end());
        if (!child->isLeaf)
            child->kids.insert(child->kids.end(), sib->kids.begin(), sib->kids.end());

        node->keys.erase(node->keys.begin() + idx);
        node->kids.erase(node->kids.begin() + idx + 1);
        delete sib;
    }

    void borrowFromPrev(Node* node, int idx) {
        Node* child = node->kids[idx];
        Node* sib   = node->kids[idx - 1];

        child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
        if (!child->isLeaf) {
            child->kids.insert(child->kids.begin(), sib->kids.back());
            sib->kids.pop_back();
        }
        node->keys[idx - 1] = sib->keys.back();
        sib->keys.pop_back();
    }

    void borrowFromNext(Node* node, int idx) {
        Node* child = node->kids[idx];
        Node* sib   = node->kids[idx + 1];

        child->keys.push_back(node->keys[idx]);
        if (!child->isLeaf) {
            child->kids.push_back(sib->kids.front());
            sib->kids.erase(sib->kids.begin());
        }
        node->keys[idx] = sib->keys.front();
        sib->keys.erase(sib->keys.begin());
    }

    // Make sure kids[idx] has at least t keys before we descend into it.
    void fill(Node* node, int idx) {
        if (idx != 0 && (int)node->kids[idx - 1]->keys.size() >= t)
            borrowFromPrev(node, idx);
        else if (idx != (int)node->keys.size() && (int)node->kids[idx + 1]->keys.size() >= t)
            borrowFromNext(node, idx);
        else if (idx != (int)node->keys.size())
            merge(node, idx);
        else
            merge(node, idx - 1);
    }

    void removeFromLeaf(Node* node, int idx) {
        node->keys.erase(node->keys.begin() + idx);
    }

    void removeFromNonLeaf(Node* node, int idx) {
        int key = node->keys[idx];
        if ((int)node->kids[idx]->keys.size() >= t) {           // 2a: predecessor
            int pred = getPred(node, idx);
            node->keys[idx] = pred;
            removeKey(node->kids[idx], pred);
        } else if ((int)node->kids[idx + 1]->keys.size() >= t) { // 2b: successor
            int succ = getSucc(node, idx);
            node->keys[idx] = succ;
            removeKey(node->kids[idx + 1], succ);
        } else {                                                 // 2c: merge then recurse
            merge(node, idx);
            removeKey(node->kids[idx], key);
        }
    }

    void removeKey(Node* node, int key) {
        int idx = 0;
        while (idx < (int)node->keys.size() && node->keys[idx] < key) ++idx;

        if (idx < (int)node->keys.size() && node->keys[idx] == key) {
            if (node->isLeaf) removeFromLeaf(node, idx);
            else              removeFromNonLeaf(node, idx);
        } else {
            if (node->isLeaf) {                  // key not in tree
                std::cout << "  (key " << key << " not found)\n";
                return;
            }
            bool last = (idx == (int)node->keys.size());
            if ((int)node->kids[idx]->keys.size() < t) fill(node, idx);

            // fill() may have shrunk node->keys via a merge; re-aim if needed.
            if (last && idx > (int)node->keys.size())
                removeKey(node->kids[idx - 1], key);
            else
                removeKey(node->kids[idx], key);
        }
    }

    void inorder(Node* node) const {
        if (!node) return;
        for (size_t i = 0; i < node->keys.size(); ++i) {
            if (!node->isLeaf) inorder(node->kids[i]);
            std::cout << node->keys[i] << ' ';
        }
        if (!node->isLeaf) inorder(node->kids.back());
    }

    void destroy(Node* node) {
        if (!node) return;
        if (!node->isLeaf)
            for (Node* c : node->kids) destroy(c);
        delete node;
    }

public:
    explicit BTree(int degree) : t(degree) {}
    ~BTree() { destroy(root); }

    void insert(int key) {
        if (!root) {
            root = new Node(true);
            root->keys.push_back(key);
            return;
        }
        if ((int)root->keys.size() == 2 * t - 1) {   // grow upward
            Node* s = new Node(false);
            s->kids.push_back(root);
            splitChild(s, 0);
            root = s;
        }
        insertNonFull(root, key);
    }

    bool search(int key) const { return root && search(root, key); }

    void remove(int key) {
        if (!root) { std::cout << "  (tree is empty)\n"; return; }
        removeKey(root, key);
        if (root->keys.empty()) {                    // root emptied — shrink
            Node* old = root;
            root = root->isLeaf ? nullptr : root->kids[0];
            delete old;
        }
    }

    void display() const {
        std::cout << "B-Tree (inorder): ";
        inorder(root);
        std::cout << '\n';
    }

    void displayLevels() const {
        std::cout << "B-Tree (level-order):\n";
        if (!root) { std::cout << "  (empty)\n"; return; }
        std::queue<std::pair<Node*, int>> q;
        q.push({root, 0});
        int curLevel = -1;
        while (!q.empty()) {
            auto [node, lvl] = q.front(); q.pop();
            if (lvl != curLevel) {
                if (curLevel != -1) std::cout << '\n';
                std::cout << "L" << lvl << ": ";
                curLevel = lvl;
            }
            std::cout << '[';
            for (size_t i = 0; i < node->keys.size(); ++i)
                std::cout << node->keys[i] << (i + 1 < node->keys.size() ? ", " : "");
            std::cout << "]  ";
            if (!node->isLeaf)
                for (Node* c : node->kids) q.push({c, lvl + 1});
        }
        std::cout << '\n';
    }
};

int main() {
    int t;
    std::cout << "Enter minimum degree (t >= 2): ";
    if (!(std::cin >> t) || t < 2) {
        std::cout << "Invalid degree.\n";
        return 1;
    }

    BTree tree(t);

    std::cout << "\nMenu:\n"
              << "  1) insert\n  2) delete\n  3) search\n"
              << "  4) inorder\n  5) level-order\n  0) exit\n";

    int choice;
    while (true) {
        std::cout << "\n> ";
        if (!(std::cin >> choice)) break;
        if (choice == 0) break;

        int key;
        switch (choice) {
            case 1:
                std::cout << "  key to insert: ";
                if (std::cin >> key) { tree.insert(key); tree.displayLevels(); }
                break;
            case 2:
                std::cout << "  key to delete: ";
                if (std::cin >> key) { tree.remove(key); tree.displayLevels(); }
                break;
            case 3:
                std::cout << "  key to search: ";
                if (std::cin >> key)
                    std::cout << "  " << key << (tree.search(key) ? " found\n" : " not found\n");
                break;
            case 4: tree.display();       break;
            case 5: tree.displayLevels(); break;
            default: std::cout << "  unknown option\n";
        }
    }
    std::cout << "bye\n";
    return 0;
}
