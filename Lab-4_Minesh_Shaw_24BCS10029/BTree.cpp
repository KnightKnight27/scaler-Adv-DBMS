#include "BTree.hpp"

// --- BTreeNode Implementation ---
BTreeNode::BTreeNode(int t1, bool leaf1) : t(t1), leaf(leaf1) {}

void BTreeNode::traverse() {
    int i;
    for (i = 0; i < keys.size(); i++) {
        if (!leaf) children[i]->traverse();
        std::cout << " " << keys[i];
    }
    if (!leaf) children[i]->traverse();
}

BTreeNode* BTreeNode::search(int k) {
    int i = 0;
    while (i < keys.size() && k > keys[i]) i++;
    if (i < keys.size() && keys[i] == k) return this;
    if (leaf) return nullptr;
    return children[i]->search(k);
}

void BTreeNode::insertNonFull(int k) {
    int i = keys.size() - 1;
    if (leaf) {
        keys.push_back(0); // placeholder
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

void BTreeNode::splitChild(int i, BTreeNode* y) {
    BTreeNode* z = new BTreeNode(y->t, y->leaf);
    for (int j = 0; j < t - 1; j++) z->keys.push_back(y->keys[j + t]);
    if (!y->leaf) {
        for (int j = 0; j < t; j++) z->children.push_back(y->children[j + t]);
    }
    y->keys.resize(t - 1);
    if (!y->leaf) y->children.resize(t);

    children.insert(children.begin() + i + 1, z);
    keys.insert(keys.begin() + i, y->keys[t - 1]);
}

void BTreeNode::remove(int k) {
    int idx = 0;
    while (idx < keys.size() && keys[idx] < k) ++idx;

    if (idx < keys.size() && keys[idx] == k) {
        if (leaf) removeFromLeaf(idx);
        else removeFromNonLeaf(idx);
    } else {
        if (leaf) return;
        bool flag = (idx == keys.size());
        if (children[idx]->keys.size() < t) fill(idx);
        if (flag && idx > keys.size()) children[idx - 1]->remove(k);
        else children[idx]->remove(k);
    }
}

void BTreeNode::removeFromLeaf(int idx) {
    keys.erase(keys.begin() + idx);
}

void BTreeNode::removeFromNonLeaf(int idx) {
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

int BTreeNode::getPred(int idx) {
    BTreeNode* cur = children[idx];
    while (!cur->leaf) cur = cur->children.back();
    return cur->keys.back();
}

int BTreeNode::getSucc(int idx) {
    BTreeNode* cur = children[idx + 1];
    while (!cur->leaf) cur = cur->children.front();
    return cur->keys.front();
}

void BTreeNode::fill(int idx) {
    if (idx != 0 && children[idx - 1]->keys.size() >= t) borrowFromPrev(idx);
    else if (idx != keys.size() && children[idx + 1]->keys.size() >= t) borrowFromNext(idx);
    else {
        if (idx != keys.size()) merge(idx);
        else merge(idx - 1);
    }
}

void BTreeNode::borrowFromPrev(int idx) {
    BTreeNode* child = children[idx];
    BTreeNode* sibling = children[idx - 1];

    child->keys.insert(child->keys.begin(), keys[idx - 1]);
    if (!child->leaf) child->children.insert(child->children.begin(), sibling->children.back());

    keys[idx - 1] = sibling->keys.back();
    sibling->keys.pop_back();
    if (!sibling->leaf) sibling->children.pop_back();
}

void BTreeNode::borrowFromNext(int idx) {
    BTreeNode* child = children[idx];
    BTreeNode* sibling = children[idx + 1];

    child->keys.push_back(keys[idx]);
    if (!child->leaf) child->children.push_back(sibling->children.front());

    keys[idx] = sibling->keys.front();
    sibling->keys.erase(sibling->keys.begin());
    if (!sibling->leaf) sibling->children.erase(sibling->children.begin());
}

void BTreeNode::merge(int idx) {
    BTreeNode* child = children[idx];
    BTreeNode* sibling = children[idx + 1];

    child->keys.push_back(keys[idx]);
    for (int i = 0; i < sibling->keys.size(); ++i) child->keys.push_back(sibling->keys[i]);
    if (!child->leaf) {
        for (int i = 0; i < sibling->children.size(); ++i) child->children.push_back(sibling->children[i]);
    }

    keys.erase(keys.begin() + idx);
    children.erase(children.begin() + idx + 1);
    delete sibling;
}


// --- BTree Implementation ---
BTree::BTree(int _t) : root(nullptr), t(_t) {}

void BTree::traverse() {
    if (root != nullptr) root->traverse();
    std::cout << "\n";
}

BTreeNode* BTree::search(int k) {
    return (root == nullptr) ? nullptr : root->search(k);
}

void BTree::insert(int k) {
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

void BTree::remove(int k) {
    if (!root) return;
    root->remove(k);
    if (root->keys.size() == 0) {
        BTreeNode* tmp = root;
        if (root->leaf) root = nullptr;
        else root = root->children[0];
        delete tmp;
    }
}