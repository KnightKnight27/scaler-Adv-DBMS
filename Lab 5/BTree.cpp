#include "BTree.h"

BTreeNode::BTreeNode(int _t, bool _leaf) {
    t = _t;
    leaf = _leaf;
    keys.reserve(2*t - 1);
    children.reserve(2*t);
}

void BTreeNode::traverse(std::ostream &out) {
    int i;
    for (i = 0; i < (int)keys.size(); i++) {
        if (!leaf) children[i]->traverse(out);
        out << keys[i] << " ";
    }
    if (!leaf) children[i]->traverse(out);
}

void BTreeNode::splitChild(int i, BTreeNode* y) {
    BTreeNode* z = new BTreeNode(y->t, y->leaf);
    z->keys.assign(y->keys.begin() + t, y->keys.end());
    y->keys.erase(y->keys.begin() + t - 1, y->keys.end());

    if (!y->leaf) {
        z->children.assign(y->children.begin() + t, y->children.end());
        y->children.erase(y->children.begin() + t, y->children.end());
    }

    children.insert(children.begin() + i + 1, z);
    keys.insert(keys.begin() + i, y->keys.back());
    y->keys.pop_back();
}

void BTreeNode::insertNonFull(int k) {
    int i = (int)keys.size() - 1;
    if (leaf) {
        keys.push_back(0);
        while (i >= 0 && keys[i] > k) {
            keys[i+1] = keys[i];
            i--;
        }
        keys[i+1] = k;
    } else {
        while (i >= 0 && keys[i] > k) i--;
        i++;
        if ((int)children[i]->keys.size() == 2*t - 1) {
            splitChild(i, children[i]);
            if (keys[i] < k) i++;
        }
        children[i]->insertNonFull(k);
    }
}

BTree::BTree(int _t) {
    root = nullptr;
    t = _t;
}

void BTree::traverse(std::ostream &out) {
    if (root != nullptr) root->traverse(out);
}

void BTree::insert(int k) {
    if (root == nullptr) {
        root = new BTreeNode(t, true);
        root->keys.push_back(k);
    } else {
        if ((int)root->keys.size() == 2*t - 1) {
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
