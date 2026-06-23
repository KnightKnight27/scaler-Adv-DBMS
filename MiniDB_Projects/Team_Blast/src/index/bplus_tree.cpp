#include "index/bplus_tree.h"
#include <iostream>
#include <algorithm>
#include <cassert>

// ─── Destructor ───────────────────────────────────────────────────────────────

BPlusTree::~BPlusTree() {
    freeTree(root_);
}

void BPlusTree::freeTree(BPlusTree::Node* node) {
    if (!node) return;
    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; ++i) {
            freeTree(node->children[i]);
        }
    }
    delete node;
}

// ─── search ───────────────────────────────────────────────────────────────────

std::optional<RecordID> BPlusTree::search(int32_t key) const {
    if (!root_) return std::nullopt;
    return searchFrom(root_, key);
}

std::optional<RecordID> BPlusTree::searchFrom(const BPlusTree::Node* node, int32_t key) const {
    if (node->is_leaf) {
        // Linear search in the leaf (ORDER is small, so this is fine)
        for (int i = 0; i < node->num_keys; ++i) {
            if (node->keys[i] == key) {
                return node->values[i];
            }
        }
        return std::nullopt;
    }

    // Internal node: find the child subtree to descend into
    int i = 0;
    while (i < node->num_keys && key >= node->keys[i]) {
        ++i;
    }
    return searchFrom(node->children[i], key);
}

BPlusTree::Node* BPlusTree::findLeaf(int32_t key) const {
    BPlusTree::Node* cur = root_;
    while (cur && !cur->is_leaf) {
        int i = 0;
        while (i < cur->num_keys && key >= cur->keys[i]) {
            ++i;
        }
        cur = cur->children[i];
    }
    return cur;
}

// ─── insert ───────────────────────────────────────────────────────────────────

void BPlusTree::insert(int32_t key, RecordID rid) {
    if (!root_) {
        // Tree is empty — create the first leaf node
        root_ = new Node();
        root_->is_leaf  = true;
        root_->num_keys = 1;
        root_->keys[0]  = key;
        root_->values[0] = rid;
        num_entries_++;
        return;
    }

    int32_t promoted_key = 0;
    BPlusTree::Node* new_right = nullptr;
    insertInto(root_, key, rid, promoted_key, new_right);

    if (new_right) {
        // Root was split — create a new root
        BPlusTree::Node* new_root = new BPlusTree::Node();
        new_root->is_leaf   = false;
        new_root->num_keys  = 1;
        new_root->keys[0]   = promoted_key;
        new_root->children[0] = root_;
        new_root->children[1] = new_right;
        root_ = new_root;
    }

    num_entries_++;
}

void BPlusTree::insertInto(BPlusTree::Node* node, int32_t key, RecordID rid,
                            int32_t& promoted_key, BPlusTree::Node*& new_right_child)
{
    new_right_child = nullptr;

    if (node->is_leaf) {
        // Check if key already exists (overwrite)
        for (int i = 0; i < node->num_keys; ++i) {
            if (node->keys[i] == key) {
                node->values[i] = rid;
                num_entries_--;  // cancel the ++ in insert()
                return;
            }
        }

        // Insert key in sorted position
        int pos = node->num_keys;
        while (pos > 0 && node->keys[pos - 1] > key) {
            node->keys[pos]   = node->keys[pos - 1];
            node->values[pos] = node->values[pos - 1];
            --pos;
        }
        node->keys[pos]   = key;
        node->values[pos] = rid;
        node->num_keys++;

        // Split if full (num_keys == ORDER)
        if (node->num_keys == BTREE_ORDER) {
            new_right_child = splitLeaf(node, promoted_key);
        }
        return;
    }

    // Internal node: descend to the appropriate child
    int i = node->num_keys;
    while (i > 0 && key < node->keys[i - 1]) {
        --i;
    }

    int32_t child_promoted = 0;
    BPlusTree::Node* child_new_right = nullptr;
    insertInto(node->children[i], key, rid, child_promoted, child_new_right);

    if (!child_new_right) {
        return;  // no split below — nothing to do here
    }

    // Insert the promoted key and new child into this internal node
    int insert_pos = node->num_keys;
    while (insert_pos > 0 && node->keys[insert_pos - 1] > child_promoted) {
        node->keys[insert_pos]         = node->keys[insert_pos - 1];
        node->children[insert_pos + 1] = node->children[insert_pos];
        --insert_pos;
    }
    node->keys[insert_pos]         = child_promoted;
    node->children[insert_pos + 1] = child_new_right;
    node->num_keys++;

    // Split this internal node if full
    if (node->num_keys == BTREE_ORDER) {
        new_right_child = splitInternal(node, promoted_key);
    }
}

// ─── splitLeaf ────────────────────────────────────────────────────────────────
//
// Splits a full leaf node in half.
// Left half keeps keys[0..mid-1], right half gets keys[mid..ORDER-1].
// promoted_key = first key of right leaf (COPIED up to parent).

BPlusTree::Node* BPlusTree::splitLeaf(BPlusTree::Node* leaf, int32_t& promoted_key) {
    int mid = BTREE_ORDER / 2;

    BPlusTree::Node* right = new BPlusTree::Node();
    right->is_leaf = true;

    // Copy the right half to the new node
    int j = 0;
    for (int i = mid; i < BTREE_ORDER; ++i, ++j) {
        right->keys[j]   = leaf->keys[i];
        right->values[j] = leaf->values[i];
    }
    right->num_keys = BTREE_ORDER - mid;
    leaf->num_keys  = mid;

    // Link the leaves
    right->next_leaf = leaf->next_leaf;
    leaf->next_leaf  = right;

    promoted_key = right->keys[0];  // COPY (not push) — leaf retains its keys
    return right;
}

// ─── splitInternal ────────────────────────────────────────────────────────────
//
// Splits a full internal node.
// Middle key is PUSHED UP to the parent (not retained in either child).
// Left keeps keys[0..mid-1] and children[0..mid].
// Right gets keys[mid+1..ORDER-1] and children[mid+1..ORDER].

BPlusTree::Node* BPlusTree::splitInternal(BPlusTree::Node* node, int32_t& promoted_key) {
    int mid = BTREE_ORDER / 2;

    promoted_key = node->keys[mid];  // this key goes up to the parent

    BPlusTree::Node* right = new BPlusTree::Node();
    right->is_leaf = false;

    // Right node gets keys[mid+1..num_keys-1]
    int j = 0;
    for (int i = mid + 1; i < node->num_keys; ++i, ++j) {
        right->keys[j]     = node->keys[i];
        right->children[j] = node->children[i];
    }
    right->children[j] = node->children[node->num_keys];
    right->num_keys     = node->num_keys - mid - 1;

    // Left node shrinks
    node->num_keys = mid;

    return right;
}

// ─── remove ───────────────────────────────────────────────────────────────────
// Simplified delete: we remove the key from the leaf but do NOT merge/borrow
// (under-flow is tolerated for demo purposes — the tree remains correct,
// just possibly has some under-full nodes).
// This keeps the implementation short and explainable.

bool BPlusTree::remove(int32_t key) {
    if (!root_) return false;

    bool found = removeFrom(root_, nullptr, -1, key);
    if (found && num_entries_ > 0) {
        num_entries_--;
    }

    // If the root became empty after a deletion, adjust the root
    if (root_ && !root_->is_leaf && root_->num_keys == 0) {
        BPlusTree::Node* old_root = root_;
        root_ = root_->children[0];
        old_root->children[0] = nullptr;
        delete old_root;
    }

    return found;
}

bool BPlusTree::removeFrom(BPlusTree::Node* node, BPlusTree::Node* parent, int child_idx, int32_t key) {
    if (node->is_leaf) {
        // Find and remove the key
        for (int i = 0; i < node->num_keys; ++i) {
            if (node->keys[i] == key) {
                // Shift remaining keys and values left
                for (int j = i; j < node->num_keys - 1; ++j) {
                    node->keys[j]   = node->keys[j + 1];
                    node->values[j] = node->values[j + 1];
                }
                node->num_keys--;

                // Update the parent's key if we removed the first key of this leaf
                if (i == 0 && parent && child_idx > 0 && node->num_keys > 0) {
                    parent->keys[child_idx - 1] = node->keys[0];
                }
                return true;
            }
        }
        return false;
    }

    // Internal node: descend
    int i = node->num_keys;
    while (i > 0 && key < node->keys[i - 1]) {
        --i;
    }
    return removeFrom(node->children[i], node, i, key);
}

// ─── scanAll ──────────────────────────────────────────────────────────────────
// Walk the leaf linked list from left to right.

void BPlusTree::scanAll(std::function<void(int32_t, const RecordID&)> callback) const {
    if (!root_) return;

    // Go to the leftmost leaf
    BPlusTree::Node* cur = root_;
    while (!cur->is_leaf) {
        cur = cur->children[0];
    }

    // Walk the linked list
    while (cur) {
        for (int i = 0; i < cur->num_keys; ++i) {
            callback(cur->keys[i], cur->values[i]);
        }
        cur = cur->next_leaf;
    }
}

// ─── printTree ────────────────────────────────────────────────────────────────
// Level-order print for demo / viva.

void BPlusTree::printTree() const {
    if (!root_) {
        std::cout << "[B+ Tree: empty]\n";
        return;
    }

    std::cout << "[B+ Tree] size=" << num_entries_ << "\n";

    // Simple level-order traversal using two vectors (current and next level)
    std::vector<BPlusTree::Node*> current_level = {root_};
    int level = 0;

    while (!current_level.empty()) {
        std::cout << "  Level " << level << ": ";
        std::vector<BPlusTree::Node*> next_level;

        for (BPlusTree::Node* node : current_level) {
            std::cout << "[";
            for (int i = 0; i < node->num_keys; ++i) {
                if (i > 0) std::cout << ",";
                std::cout << node->keys[i];
            }
            std::cout << "]";
            if (node->is_leaf) std::cout << "L";
            std::cout << " ";

            if (!node->is_leaf) {
                for (int i = 0; i <= node->num_keys; ++i) {
                    if (node->children[i]) {
                        next_level.push_back(node->children[i]);
                    }
                }
            }
        }
        std::cout << "\n";

        if (next_level.empty()) break;
        current_level = next_level;
        ++level;
    }

    // Print leaf chain
    std::cout << "  Leaf chain: ";
    BPlusTree::Node* leaf = root_;
    while (leaf && !leaf->is_leaf) leaf = leaf->children[0];
    while (leaf) {
        std::cout << "[";
        for (int i = 0; i < leaf->num_keys; ++i) {
            if (i > 0) std::cout << ",";
            std::cout << leaf->keys[i];
        }
        std::cout << "]→";
        leaf = leaf->next_leaf;
    }
    std::cout << "NULL\n";
}
