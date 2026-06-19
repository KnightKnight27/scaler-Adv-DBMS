#ifndef BTREE_H
#define BTREE_H

#include <iostream>
#include <vector>

class BTreeNode {
public:
    std::vector<int>        keys;
    std::vector<BTreeNode*> children;
    int  t;
    bool leaf;

    BTreeNode(int degree, bool isLeaf) : t(degree), leaf(isLeaf) {}

    ~BTreeNode() {
        for (auto* c : children)
            delete c;
    }

    void traverse() {
        int n = static_cast<int>(keys.size());
        for (int i = 0; i < n; ++i) {
            if (!leaf)
                children[i]->traverse();
            std::cout << keys[i] << " ";
        }
        if (!leaf)
            children[n]->traverse();
    }
    BTreeNode* search(int k) {
        int i = 0;
        int n = static_cast<int>(keys.size());
        while (i < n && k > keys[i])
            ++i;
        if (i < n && keys[i] == k)
            return this;
        if (leaf)
            return nullptr;
        return children[i]->search(k);
    }

    int findKey(int k) {
        int idx = 0;
        int n   = static_cast<int>(keys.size());
        while (idx < n && keys[idx] < k)
            ++idx;
        return idx;
    }
    void splitChild(int i, BTreeNode* y) {
        int medianKey = y->keys[t - 1];

        BTreeNode* z = new BTreeNode(y->t, y->leaf);
        for (int j = t; j <= 2 * t - 2; ++j)
            z->keys.push_back(y->keys[j]);

        if (!y->leaf) {
            for (int j = t; j <= 2 * t - 1; ++j)
                z->children.push_back(y->children[j]);
            y->children.resize(t);       
        }

        y->keys.resize(t - 1);


        children.insert(children.begin() + i + 1, z);
        keys.insert(keys.begin() + i, medianKey);
    }

    void insertNonFull(int k) {
        int i = static_cast<int>(keys.size()) - 1;

        if (leaf) {
            keys.push_back(0);
            while (i >= 0 && keys[i] > k) {
                keys[i + 1] = keys[i];
                --i;
            }
            keys[i + 1] = k;
        } else {
            while (i >= 0 && keys[i] > k)
                --i;
            ++i;
            if (static_cast<int>(children[i]->keys.size()) == 2 * t - 1) {
                splitChild(i, children[i]);
                if (keys[i] < k)
                    ++i;
            }
            children[i]->insertNonFull(k);
        }
    }


    int getPred(int idx) {
        BTreeNode* cur = children[idx];
        while (!cur->leaf)
            cur = cur->children.back();
        return cur->keys.back();
    }


    int getSucc(int idx) {
        BTreeNode* cur = children[idx + 1];
        while (!cur->leaf)
            cur = cur->children.front();
        return cur->keys.front();
    }

    void removeFromLeaf(int idx) {
        keys.erase(keys.begin() + idx);
    }


    void removeFromNonLeaf(int idx) {
        int k = keys[idx];

        if (static_cast<int>(children[idx]->keys.size()) >= t) {
            int pred  = getPred(idx);
            keys[idx] = pred;
            children[idx]->remove(pred);
        } else if (static_cast<int>(children[idx + 1]->keys.size()) >= t) {
            int succ  = getSucc(idx);
            keys[idx] = succ;
            children[idx + 1]->remove(succ);
        } else {
            merge(idx);
            children[idx]->remove(k);
        }
    }

  
    void borrowFromPrev(int idx) {
        BTreeNode* child   = children[idx];
        BTreeNode* sibling = children[idx - 1];

        child->keys.insert(child->keys.begin(), keys[idx - 1]);
        if (!child->leaf)
            child->children.insert(child->children.begin(),
                                   sibling->children.back());

        keys[idx - 1] = sibling->keys.back();
        sibling->keys.pop_back();
        if (!sibling->leaf)
            sibling->children.pop_back();
    }

    void borrowFromNext(int idx) {
        BTreeNode* child   = children[idx];
        BTreeNode* sibling = children[idx + 1];

        child->keys.push_back(keys[idx]);
        if (!child->leaf)
            child->children.push_back(sibling->children.front());

        keys[idx] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());
        if (!sibling->leaf)
            sibling->children.erase(sibling->children.begin());
    }

 
    void merge(int idx) {
        BTreeNode* child   = children[idx];
        BTreeNode* sibling = children[idx + 1];

        child->keys.push_back(keys[idx]);

        for (int k : sibling->keys)
            child->keys.push_back(k);
        if (!child->leaf)
            for (auto* c : sibling->children)
                child->children.push_back(c);


        sibling->children.clear();

        keys.erase(keys.begin() + idx);
        children.erase(children.begin() + idx + 1);

        delete sibling;
    }


    void fill(int idx) {
        int n = static_cast<int>(keys.size());

        if (idx != 0 &&
            static_cast<int>(children[idx - 1]->keys.size()) >= t) {
            borrowFromPrev(idx);
        } else if (idx != n &&
                   static_cast<int>(children[idx + 1]->keys.size()) >= t) {
            borrowFromNext(idx);
        } else {
            if (idx != n)
                merge(idx);
            else
                merge(idx - 1);
        }
    }

    void remove(int k) {
        int idx = findKey(k);
        int n   = static_cast<int>(keys.size());

        if (idx < n && keys[idx] == k) {
            if (leaf)
                removeFromLeaf(idx);
            else
                removeFromNonLeaf(idx);
        } else {
            if (leaf)
                return;   

            bool lastChild = (idx == n);

            if (static_cast<int>(children[idx]->keys.size()) < t)
                fill(idx);

            if (lastChild && idx > static_cast<int>(keys.size()))
                children[idx - 1]->remove(k);
            else
                children[idx]->remove(k);
        }
    }
};
class BTree {
    BTreeNode* root;
    int t;

public:
    explicit BTree(int degree) : root(nullptr), t(degree) {}

    ~BTree() { delete root; }


    void traverse() {
        if (root) {
            root->traverse();
            std::cout << '\n';
        }
    }

    BTreeNode* search(int k) {
        return root ? root->search(k) : nullptr;
    }

  
    void insert(int k) {
        if (!root) {
            root = new BTreeNode(t, true);
            root->keys.push_back(k);
            return;
        }

        if (static_cast<int>(root->keys.size()) == 2 * t - 1) {
            BTreeNode* s = new BTreeNode(t, false);
            s->children.push_back(root);
            s->splitChild(0, root);
            int i = (s->keys[0] < k) ? 1 : 0;
            s->children[i]->insertNonFull(k);
            root = s;
        } else {
            root->insertNonFull(k);
        }
    }

    void remove(int k) {
        if (!root) return;

        root->remove(k);


        if (root->keys.empty()) {
            BTreeNode* oldRoot = root;
            root = root->leaf ? nullptr : root->children.front();
            oldRoot->children.clear();   
            delete oldRoot;
        }
    }
};

#endif 