#include "bplus_tree.h"
#include <algorithm>

BPlusTree::BPlusTree() : max_keys(2 * BP_T - 1) {
    root = new BPNode(true); // empty leaf node = empty tree
}

BPlusTree::~BPlusTree() {
    freeNode(root);
}

void BPlusTree::freeNode(BPNode* node) {
    if (!node->is_leaf) {
        for (BPNode* child : node->children) freeNode(child);
    }
    delete node;
}

// Walk from root down to the leaf that would contain 'key'.
BPNode* BPlusTree::findLeaf(int key) {
    BPNode* cur = root;
    while (!cur->is_leaf) {
        // Find the first key in 'cur' that is greater than 'key'.
        // We go into the child just before that key.
        int i = 0;
        while (i < (int)cur->keys.size() && key >= cur->keys[i]) i++;
        cur = cur->children[i];
    }
    return cur;
}

bool BPlusTree::contains(int key) {
    RID dummy;
    return search(key, dummy);
}

bool BPlusTree::search(int key, RID& out) {
    BPNode* leaf = findLeaf(key);
    for (int i = 0; i < (int)leaf->keys.size(); i++) {
        if (leaf->keys[i] == key) {
            out = leaf->rids[i];
            return true;
        }
    }
    return false;
}

std::vector<RID> BPlusTree::rangeSearch(int lo, int hi) {
    std::vector<RID> result;
    BPNode* leaf = findLeaf(lo);
    // Walk the linked leaf chain until we pass 'hi'.
    while (leaf != nullptr) {
        for (int i = 0; i < (int)leaf->keys.size(); i++) {
            if (leaf->keys[i] > hi) return result;
            if (leaf->keys[i] >= lo) result.push_back(leaf->rids[i]);
        }
        leaf = leaf->next;
    }
    return result;
}

// ---- Insert implementation ----
//
// We recurse down the tree. At each level, if the node splits we pass a
// "split result" (the pushed-up key + new right child) back to the parent.

// Helper: insert key+rid into a leaf at the correct sorted position.
static void leafInsert(BPNode* leaf, int key, RID rid) {
    int i = 0;
    while (i < (int)leaf->keys.size() && leaf->keys[i] < key) i++;
    leaf->keys.insert(leaf->keys.begin() + i, key);
    leaf->rids.insert(leaf->rids.begin() + i, rid);
}

BPlusTree::Split BPlusTree::insertRec(BPNode* node, int key, RID rid) {
    if (node->is_leaf) {
        leafInsert(node, key, rid);

        if ((int)node->keys.size() <= max_keys) {
            return {0, nullptr}; // no overflow, no split needed
        }

        // Leaf overflow: split into two halves.
        int mid = (int)node->keys.size() / 2;
        BPNode* right = new BPNode(true);

        // Upper half goes into the new right leaf.
        right->keys.assign(node->keys.begin() + mid, node->keys.end());
        right->rids.assign(node->rids.begin() + mid, node->rids.end());
        node->keys.resize(mid);
        node->rids.resize(mid);

        // Insert the new right leaf into the leaf chain.
        right->next = node->next;
        node->next  = right;

        // "Copy-up": the first key of the right leaf goes to the parent
        // but also stays in the right leaf (B+ Tree rule).
        return {right->keys[0], right};
    }

    // Internal node: find the right child to recurse into.
    int i = 0;
    while (i < (int)node->keys.size() && key >= node->keys[i]) i++;
    Split sr = insertRec(node->children[i], key, rid);

    if (sr.right == nullptr) return {0, nullptr}; // no split below

    // The child split: absorb the pushed-up key and the new right child.
    node->keys.insert(node->keys.begin() + i, sr.key);
    node->children.insert(node->children.begin() + i + 1, sr.right);

    if ((int)node->keys.size() <= max_keys) {
        return {0, nullptr}; // no overflow here
    }

    // Internal overflow: split this node too.
    int mid    = (int)node->keys.size() / 2;
    int pushup = node->keys[mid]; // this key goes up and is removed here

    BPNode* right = new BPNode(false);
    right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    right->children.assign(node->children.begin() + mid + 1, node->children.end());
    node->keys.resize(mid);
    node->children.resize(mid + 1);

    return {pushup, right};
}

void BPlusTree::insert(int key, RID rid) {
    Split sr = insertRec(root, key, rid);
    if (sr.right != nullptr) {
        // The root itself split — create a new root above it.
        BPNode* new_root = new BPNode(false);
        new_root->keys.push_back(sr.key);
        new_root->children.push_back(root);
        new_root->children.push_back(sr.right);
        root = new_root;
    }
}

void BPlusTree::remove(int key) {
    BPNode* leaf = findLeaf(key);
    for (int i = 0; i < (int)leaf->keys.size(); i++) {
        if (leaf->keys[i] == key) {
            leaf->keys.erase(leaf->keys.begin() + i);
            leaf->rids.erase(leaf->rids.begin() + i);
            return;
        }
    }
    // Key not in tree — nothing to do.
}
