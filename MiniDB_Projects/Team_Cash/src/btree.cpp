#include "btree.h"

namespace minidb {

BPlusTree::Node* BPlusTree::findLeaf(int64_t key) const {
    Node* node = root_.get();
    while (!node->leaf) {
        size_t i = 0;
        while (i < node->keys.size() && key >= node->keys[i]) ++i;
        node = node->children[i].get();
    }
    return node;
}

BPlusTree::Node* BPlusTree::leftmostLeaf() const {
    Node* node = root_.get();
    while (!node->leaf) node = node->children[0].get();
    return node;
}

bool BPlusTree::search(int64_t key, RID& out) const {
    Node* leaf = findLeaf(key);
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
        if (leaf->keys[i] == key) {
            out = leaf->rids[i];
            return true;
        }
    }
    return false;
}

std::vector<std::pair<int64_t, RID>> BPlusTree::rangeScan(int64_t low, int64_t high) const {
    std::vector<std::pair<int64_t, RID>> result;
    Node* leaf = findLeaf(low);
    while (leaf != nullptr) {
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            int64_t k = leaf->keys[i];
            if (k > high) return result;
            if (k >= low) result.emplace_back(k, leaf->rids[i]);
        }
        leaf = leaf->next;
    }
    return result;
}

void BPlusTree::insert(int64_t key, RID rid) {
    if (static_cast<int>(root_->keys.size()) >= order_) {  // root full -> grow taller
        auto newRoot = std::make_unique<Node>(false);
        newRoot->children.push_back(std::move(root_));
        splitChild(newRoot.get(), 0);
        root_ = std::move(newRoot);
    }
    insertNonFull(root_.get(), key, rid);
}

void BPlusTree::insertNonFull(Node* node, int64_t key, RID rid) {
    if (node->leaf) {
        size_t i = 0;
        while (i < node->keys.size() && node->keys[i] < key) ++i;
        if (i < node->keys.size() && node->keys[i] == key) {
            node->rids[i] = rid;  // key already present -> replace RID
            return;
        }
        node->keys.insert(node->keys.begin() + i, key);
        node->rids.insert(node->rids.begin() + i, rid);
        return;
    }
    size_t i = 0;
    while (i < node->keys.size() && key >= node->keys[i]) ++i;
    if (static_cast<int>(node->children[i]->keys.size()) >= order_) {
        splitChild(node, static_cast<int>(i));
        if (key >= node->keys[i]) ++i;
    }
    insertNonFull(node->children[i].get(), key, rid);
}

void BPlusTree::splitChild(Node* parent, int index) {
    Node* child = parent->children[index].get();
    int mid = static_cast<int>(child->keys.size()) / 2;

    if (child->leaf) {
        auto newLeaf = std::make_unique<Node>(true);
        for (int i = mid; i < static_cast<int>(child->keys.size()); ++i) {
            newLeaf->keys.push_back(child->keys[i]);
            newLeaf->rids.push_back(child->rids[i]);
        }
        child->keys.resize(mid);
        child->rids.resize(mid);
        newLeaf->next = child->next;     // keep the leaf chain linked
        child->next = newLeaf.get();
        int64_t copyUp = newLeaf->keys.front();
        parent->keys.insert(parent->keys.begin() + index, copyUp);
        parent->children.insert(parent->children.begin() + index + 1, std::move(newLeaf));
    } else {
        auto newNode = std::make_unique<Node>(false);
        int64_t upKey = child->keys[mid];
        for (int i = mid + 1; i < static_cast<int>(child->keys.size()); ++i)
            newNode->keys.push_back(child->keys[i]);
        for (int i = mid + 1; i < static_cast<int>(child->children.size()); ++i)
            newNode->children.push_back(std::move(child->children[i]));
        child->keys.resize(mid);
        child->children.resize(mid + 1);
        parent->keys.insert(parent->keys.begin() + index, upKey);
        parent->children.insert(parent->children.begin() + index + 1, std::move(newNode));
    }
}

bool BPlusTree::erase(int64_t key) {
    // Lazy delete: remove the entry from its leaf, but do not merge or
    // rebalance underfull nodes. Routing keys still lead searches to the right
    // leaf, so lookups and range scans stay correct.
    Node* leaf = findLeaf(key);
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
        if (leaf->keys[i] == key) {
            leaf->keys.erase(leaf->keys.begin() + i);
            leaf->rids.erase(leaf->rids.begin() + i);
            return true;
        }
    }
    return false;
}

int BPlusTree::height() const {
    int h = 1;
    Node* node = root_.get();
    while (!node->leaf) {
        node = node->children[0].get();
        ++h;
    }
    return h;
}

std::vector<std::pair<int64_t, RID>> BPlusTree::items() const {
    std::vector<std::pair<int64_t, RID>> out;
    Node* node = leftmostLeaf();
    while (node != nullptr) {
        for (size_t i = 0; i < node->keys.size(); ++i)
            out.emplace_back(node->keys[i], node->rids[i]);
        node = node->next;
    }
    return out;
}

}  // namespace minidb
