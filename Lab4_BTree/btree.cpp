#include <iostream>
#include <vector>
#include <algorithm>
#include <cassert>

class BTreeNode {
public:
    int t; // Minimum degree
    std::vector<int> keys;
    std::vector<BTreeNode*> children;
    bool is_leaf;

    BTreeNode(int t, bool is_leaf) : t(t), is_leaf(is_leaf) {}

    void traverse(int depth = 0) {
        int i;
        for (i = 0; i < keys.size(); i++) {
            if (!is_leaf) children[i]->traverse(depth + 1);
            for (int d = 0; d < depth; ++d) std::cout << "  ";
            std::cout << keys[i] << std::endl;
        }
        if (!is_leaf) children[i]->traverse(depth + 1);
    }

    BTreeNode* search(int k) {
        int i = 0;
        while (i < keys.size() && k > keys[i]) i++;
        if (i < keys.size() && keys[i] == k) return this;
        if (is_leaf) return nullptr;
        return children[i]->search(k);
    }

    int findKey(int k) {
        int idx = 0;
        while (idx < keys.size() && keys[idx] < k) ++idx;
        return idx;
    }

    void insertNonFull(int k) {
        int i = keys.size() - 1;
        if (is_leaf) {
            keys.push_back(0);
            while (i >= 0 && keys[i] > k) {
                keys[i + 1] = keys[i];
                i--;
            }
            keys[i + 1] = k;
        } else {
            while (i >= 0 && keys[i] > k) i--;
            if (children[i + 1]->keys.size() == 2 * t - 1) {
                splitChild(i + 1, children[i + 1]);
                if (keys[i + 1] < k) i++;
            }
            children[i + 1]->insertNonFull(k);
        }
    }

    void splitChild(int i, BTreeNode* y) {
        BTreeNode* z = new BTreeNode(y->t, y->is_leaf);
        z->keys.resize(t - 1);
        for (int j = 0; j < t - 1; j++) {
            z->keys[j] = y->keys[j + t];
        }
        if (!y->is_leaf) {
            z->children.resize(t);
            for (int j = 0; j < t; j++) {
                z->children[j] = y->children[j + t];
            }
        }
        int promoted_key = y->keys[t - 1];
        y->keys.resize(t - 1);
        if (!y->is_leaf) {
            y->children.resize(t);
        }

        children.insert(children.begin() + i + 1, z);
        keys.insert(keys.begin() + i, promoted_key);
    }

    void remove(int k) {
        int idx = findKey(k);
        if (idx < keys.size() && keys[idx] == k) {
            if (is_leaf) {
                removeFromLeaf(idx);
            } else {
                removeFromNonLeaf(idx);
            }
        } else {
            if (is_leaf) {
                std::cout << "The key " << k << " does not exist in the tree." << std::endl;
                return;
            }
            bool flag = (idx == keys.size());
            if (children[idx]->keys.size() < t) {
                fill(idx);
            }
            if (flag && idx > keys.size()) {
                children[idx - 1]->remove(k);
            } else {
                children[idx]->remove(k);
            }
        }
    }

    void removeFromLeaf(int idx) {
        keys.erase(keys.begin() + idx);
    }

    void removeFromNonLeaf(int idx) {
        int k = keys[idx];
        if (children[idx]->keys.size() >= t) {
            int pred = getPred(idx);
            keys[idx] = pred;
            children[idx]->remove(pred);
        } else if (children[idx + 1]->keys.size() >= t) {
            int succ = getSucc(idx);
            keys[idx] = succ;
            children[idx + 1]->remove(succ);
        } else {
            merge(idx);
            children[idx]->remove(k);
        }
    }

    int getPred(int idx) {
        BTreeNode* cur = children[idx];
        while (!cur->is_leaf) {
            cur = cur->children[cur->keys.size()];
        }
        return cur->keys.back();
    }

    int getSucc(int idx) {
        BTreeNode* cur = children[idx + 1];
        while (!cur->is_leaf) {
            cur = cur->children[0];
        }
        return cur->keys[0];
    }

    void fill(int idx) {
        if (idx != 0 && children[idx - 1]->keys.size() >= t) {
            borrowFromPrev(idx);
        } else if (idx != keys.size() && children[idx + 1]->keys.size() >= t) {
            borrowFromNext(idx);
        } else {
            if (idx != keys.size()) {
                merge(idx);
            } else {
                merge(idx - 1);
            }
        }
    }

    void borrowFromPrev(int idx) {
        BTreeNode* child = children[idx];
        BTreeNode* sibling = children[idx - 1];

        child->keys.insert(child->keys.begin(), keys[idx - 1]);
        if (!child->is_leaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }
        keys[idx - 1] = sibling->keys.back();
        sibling->keys.pop_back();
    }

    void borrowFromNext(int idx) {
        BTreeNode* child = children[idx];
        BTreeNode* sibling = children[idx + 1];

        child->keys.push_back(keys[idx]);
        keys[idx] = sibling->keys[0];
        sibling->keys.erase(sibling->keys.begin());

        if (!child->is_leaf) {
            child->children.push_back(sibling->children[0]);
            sibling->children.erase(sibling->children.begin());
        }
    }

    void merge(int idx) {
        BTreeNode* child = children[idx];
        BTreeNode* sibling = children[idx + 1];

        child->keys.push_back(keys[idx]);
        for (int i = 0; i < sibling->keys.size(); i++) {
            child->keys.push_back(sibling->keys[i]);
        }
        if (!child->is_leaf) {
            for (int i = 0; i <= sibling->keys.size(); i++) {
                child->children.push_back(sibling->children[i]);
            }
        }
        keys.erase(keys.begin() + idx);
        children.erase(children.begin() + idx + 1);
        delete sibling;
    }
};

class BTree {
public:
    BTreeNode* root;
    int t;

    BTree(int t) : root(nullptr), t(t) {}

    void traverse() {
        if (root != nullptr) root->traverse();
    }

    BTreeNode* search(int k) {
        return (root == nullptr) ? nullptr : root->search(k);
    }

    void insert(int k) {
        if (root == nullptr) {
            root = new BTreeNode(t, true);
            root->keys.push_back(k);
        } else {
            if (root->keys.size() == 2 * t - 1) {
                BTreeNode* s = new BTreeNode(t, false);
                s->children.push_back(root);
                s->splitChild(0, root);
                int i = 0;
                if (s->keys[0] < k) i++;
                s->children[i]->insertNonFull(k);
                root = s;
            } else {
                root->insertNonFull(k);
            }
        }
    }

    void remove(int k) {
        if (!root) {
            std::cout << "The tree is empty." << std::endl;
            return;
        }
        root->remove(k);
        if (root->keys.empty()) {
            BTreeNode* tmp = root;
            if (root->is_leaf) {
                root = nullptr;
            } else {
                root = root->children[0];
            }
            delete tmp;
        }
    }
};

int main() {
    std::cout << "=== Starting Lab 4: B-Tree Complete Implementation ===" << std::endl;
    BTree t(3); 

    std::cout << "Inserting keys 1 to 10..." << std::endl;
    for (int i = 1; i <= 10; ++i) {
        t.insert(i);
    }

    std::cout << "Traversal of the constructed tree:" << std::endl;
    t.traverse();
    std::cout << std::endl;

    std::cout << "Searching for key 6..." << std::endl;
    BTreeNode* node = t.search(6);
    if (node) {
        std::cout << "Key 6 found!" << std::endl;
    } else {
        std::cout << "Key 6 not found." << std::endl;
    }

    std::cout << "Removing key 6..." << std::endl;
    t.remove(6);
    t.traverse();
    std::cout << std::endl;

    std::cout << "Removing key 3..." << std::endl;
    t.remove(3);
    t.traverse();
    std::cout << std::endl;

    std::cout << "Removing key 1..." << std::endl;
    t.remove(1);
    t.traverse();
    std::cout << std::endl;

    return 0;
}
