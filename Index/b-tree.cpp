#include <iostream>
#include <vector>
#include <string>
#include <climits>
using namespace std;

// B-Tree of minimum degree t:
//   every node has at most 2t-1 keys and 2t children
//   every non-root node has at least t-1 keys

template <typename Key, typename Row>
class DB {
private:
    struct Entry {
        Key key;
        Row  row;
        Entry() {}
        Entry(Key k, Row r) : key(k), row(r) {}
    };

    struct BTreeNode {
        vector<Entry>     keys;
        vector<BTreeNode*> children;
        bool isLeaf;
        BTreeNode(bool leaf) : isLeaf(leaf) {}
    };

    BTreeNode* root;
    size_t t; // minimum degree

    bool isFull(BTreeNode* n) { return n->keys.size() == 2 * t - 1; }

    // ── Search ───────────────────────────────────────────────────────────
    Entry* searchHelper(BTreeNode* node, Key key) {
        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i].key) i++;

        if (i < (int)node->keys.size() && key == node->keys[i].key)
            return &node->keys[i];

        if (node->isLeaf) return nullptr;

        return searchHelper(node->children[i], key);
    }

    // ── Split ────────────────────────────────────────────────────────────
    // Split the full child parent->children[i] and push its median up.
    void splitChild(BTreeNode* parent, int i) {
        BTreeNode* full = parent->children[i];
        BTreeNode* right = new BTreeNode(full->isLeaf);

        Entry median = full->keys[t - 1];

        right->keys.assign(full->keys.begin() + t, full->keys.end());
        full->keys.resize(t - 1);

        if (!full->isLeaf) {
            right->children.assign(full->children.begin() + t, full->children.end());
            full->children.resize(t);
        }

        parent->children.insert(parent->children.begin() + i + 1, right);
        parent->keys.insert(parent->keys.begin() + i, median);
    }

    // ── Insert into a node known to be non-full ──────────────────────────
    void insertNonFull(BTreeNode* node, Entry entry) {
        int i = (int)node->keys.size() - 1;

        if (node->isLeaf) {
            node->keys.push_back(Entry());
            while (i >= 0 && entry.key < node->keys[i].key) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = entry;
        } else {
            while (i >= 0 && entry.key < node->keys[i].key) i--;
            i++;
            if (isFull(node->children[i])) {
                splitChild(node, i);
                if (entry.key > node->keys[i].key) i++;
            }
            insertNonFull(node->children[i], entry);
        }
    }

    // ── Delete helpers ───────────────────────────────────────────────────
    Entry getPredecessor(BTreeNode* node, int idx) {
        BTreeNode* cur = node->children[idx];
        while (!cur->isLeaf) cur = cur->children.back();
        return cur->keys.back();
    }

    Entry getSuccessor(BTreeNode* node, int idx) {
        BTreeNode* cur = node->children[idx + 1];
        while (!cur->isLeaf) cur = cur->children.front();
        return cur->keys.front();
    }

    // Merge children[idx] and children[idx+1], pulling parent->keys[idx] down.
    void merge(BTreeNode* node, int idx) {
        BTreeNode* left  = node->children[idx];
        BTreeNode* right = node->children[idx + 1];

        left->keys.push_back(node->keys[idx]);
        for (auto& k : right->keys)     left->keys.push_back(k);
        for (auto* c : right->children) left->children.push_back(c);

        node->keys.erase(node->keys.begin() + idx);
        node->children.erase(node->children.begin() + idx + 1);
        delete right;
    }

    void borrowFromLeft(BTreeNode* node, int idx) {
        BTreeNode* child  = node->children[idx];
        BTreeNode* left   = node->children[idx - 1];

        child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
        if (!left->isLeaf) {
            child->children.insert(child->children.begin(), left->children.back());
            left->children.pop_back();
        }
        node->keys[idx - 1] = left->keys.back();
        left->keys.pop_back();
    }

    void borrowFromRight(BTreeNode* node, int idx) {
        BTreeNode* child  = node->children[idx];
        BTreeNode* right  = node->children[idx + 1];

        child->keys.push_back(node->keys[idx]);
        if (!right->isLeaf) {
            child->children.push_back(right->children.front());
            right->children.erase(right->children.begin());
        }
        node->keys[idx] = right->keys.front();
        right->keys.erase(right->keys.begin());
    }

    // Ensure children[idx] has at least t keys before descending.
    void fixChild(BTreeNode* node, int idx) {
        if (idx > 0 && node->children[idx-1]->keys.size() >= t)
            borrowFromLeft(node, idx);
        else if (idx < (int)node->children.size()-1 &&
                 node->children[idx+1]->keys.size() >= t)
            borrowFromRight(node, idx);
        else {
            if (idx < (int)node->children.size()-1) merge(node, idx);
            else                                     merge(node, idx-1);
        }
    }

    void deleteHelper(BTreeNode* node, Key key) {
        int idx = 0;
        while (idx < (int)node->keys.size() && key > node->keys[idx].key) idx++;

        bool found = (idx < (int)node->keys.size() && node->keys[idx].key == key);

        if (found) {
            if (node->isLeaf) {
                node->keys.erase(node->keys.begin() + idx);
            } else if (node->children[idx]->keys.size() >= t) {
                Entry pred = getPredecessor(node, idx);
                node->keys[idx] = pred;
                deleteHelper(node->children[idx], pred.key);
            } else if (node->children[idx+1]->keys.size() >= t) {
                Entry succ = getSuccessor(node, idx);
                node->keys[idx] = succ;
                deleteHelper(node->children[idx+1], succ.key);
            } else {
                merge(node, idx);
                deleteHelper(node->children[idx], key);
            }
        } else {
            if (node->isLeaf) { cout << "Key not found\n"; return; }

            bool isLast = (idx == (int)node->keys.size());
            if (node->children[idx]->keys.size() < t) {
                fixChild(node, idx);
                if (isLast && idx > (int)node->keys.size()) idx--;
            }
            deleteHelper(node->children[idx], key);
        }
    }

    // ── Print ────────────────────────────────────────────────────────────
    void printHelper(BTreeNode* node, int level) {
        if (!node) return;
        cout << string(level * 4, ' ') << "[";
        for (int i = 0; i < (int)node->keys.size(); i++) {
            if (i) cout << " | ";
            cout << node->keys[i].key;
        }
        cout << "]\n";
        for (auto* child : node->children)
            printHelper(child, level + 1);
    }

    void destroyTree(BTreeNode* node) {
        if (!node) return;
        for (auto* c : node->children) destroyTree(c);
        delete node;
    }

public:
    DB(size_t degree) : t(degree), root(nullptr) {}
    ~DB() { destroyTree(root); }

    // ── Public API ───────────────────────────────────────────────────────
    void Insert(Key key, Row row) {
        Entry entry(key, row);
        if (!root) {
            root = new BTreeNode(true);
            root->keys.push_back(entry);
            return;
        }
        if (isFull(root)) {
            BTreeNode* newRoot = new BTreeNode(false);
            newRoot->children.push_back(root);
            splitChild(newRoot, 0);
            root = newRoot;
        }
        insertNonFull(root, entry);
    }

    Row* Search(Key key) {
        if (!root) return nullptr;
        Entry* e = searchHelper(root, key);
        return e ? &e->row : nullptr;
    }

    void Delete(Key key) {
        if (!root) { cout << "Tree is empty\n"; return; }
        deleteHelper(root, key);
        if (root->keys.empty()) {
            BTreeNode* oldRoot = root;
            root = root->isLeaf ? nullptr : root->children[0];
            delete oldRoot;
        }
    }

    void Print() {
        cout << "B-Tree (t=" << t << "):\n";
        printHelper(root, 0);
        cout << "\n";
    }
};

int main() {
    DB<int, string> db(3); // minimum degree = 3

    for (auto [k, v] : vector<pair<int,string>>{
            {10,"Row_10"},{20,"Row_20"},{5,"Row_5"},{6,"Row_6"},
            {12,"Row_12"},{30,"Row_30"},{7,"Row_7"},{17,"Row_17"},
            {3,"Row_3"},{1,"Row_1"},{25,"Row_25"}})
        db.Insert(k, v);

    db.Print();

    cout << "Search 12 -> ";
    auto* r = db.Search(12);
    cout << (r ? *r : "not found") << "\n";

    cout << "Search 99 -> ";
    r = db.Search(99);
    cout << (r ? *r : "not found") << "\n\n";

    db.Delete(6);
    cout << "After deleting 6:\n"; db.Print();

    db.Delete(20);
    cout << "After deleting 20:\n"; db.Print();

    db.Delete(10);
    cout << "After deleting 10:\n"; db.Print();

    return 0;
}
