#include "bplus_tree.h"

#include <algorithm>

#include "../catalog/catalog.h"
#include "../common/tuple.h"

namespace minidb {

int BPlusTree::ChildIndex(const Node* n, const Value& key) {
    // Separator keys[i] is the smallest key in subtree kids[i+1]. Walk to the
    // largest separator <= key; descend into that child.
    int idx = 0;
    while (idx < static_cast<int>(n->keys.size()) && !(key < n->keys[idx])) ++idx;
    return idx;
}

bool BPlusTree::InsertRec(Node* n, const Value& key, const RID& rid, Value* promo, Node** right) {
    if (n->leaf) {
        // Locate the key; append RID if present, otherwise insert in sorted order.
        int pos = 0;
        while (pos < static_cast<int>(n->keys.size()) && n->keys[pos] < key) ++pos;
        if (pos < static_cast<int>(n->keys.size()) && n->keys[pos] == key) {
            n->vals[pos].push_back(rid);
            return false;
        }
        n->keys.insert(n->keys.begin() + pos, key);
        n->vals.insert(n->vals.begin() + pos, std::vector<RID>{rid});
        if (static_cast<int>(n->keys.size()) <= MAX_KEYS) return false;

        // Split leaf: right half moves to a new leaf; first right key is copied up.
        int mid = static_cast<int>(n->keys.size()) / 2;
        Node* r = new Node(true);
        r->keys.assign(n->keys.begin() + mid, n->keys.end());
        r->vals.assign(n->vals.begin() + mid, n->vals.end());
        n->keys.resize(mid);
        n->vals.resize(mid);
        r->next = n->next;
        n->next = r;
        *promo = r->keys.front();
        *right = r;
        return true;
    }

    int ci = ChildIndex(n, key);
    Value cpromo;
    Node* cright = nullptr;
    if (!InsertRec(n->kids[ci], key, rid, &cpromo, &cright)) return false;

    // Child split: insert its promoted separator and new right child here.
    n->keys.insert(n->keys.begin() + ci, cpromo);
    n->kids.insert(n->kids.begin() + ci + 1, cright);
    if (static_cast<int>(n->keys.size()) <= MAX_KEYS) return false;

    // Split internal node: middle separator is pushed up (not copied).
    int mid = static_cast<int>(n->keys.size()) / 2;
    Node* r = new Node(false);
    *promo = n->keys[mid];
    r->keys.assign(n->keys.begin() + mid + 1, n->keys.end());
    r->kids.assign(n->kids.begin() + mid + 1, n->kids.end());
    n->keys.resize(mid);
    n->kids.resize(mid + 1);
    *right = r;
    return true;
}

void BPlusTree::Insert(const Value& key, const RID& rid) {
    Value promo;
    Node* right = nullptr;
    if (InsertRec(root_, key, rid, &promo, &right)) {
        Node* new_root = new Node(false);
        new_root->keys.push_back(promo);
        new_root->kids.push_back(root_);
        new_root->kids.push_back(right);
        root_ = new_root;
    }
}

const BPlusTree::Node* BPlusTree::FindLeaf(const Value& key) const {
    const Node* n = root_;
    while (!n->leaf) n = n->kids[ChildIndex(n, key)];
    return n;
}

std::vector<RID> BPlusTree::Search(const Value& key) const {
    const Node* leaf = FindLeaf(key);
    for (size_t i = 0; i < leaf->keys.size(); ++i)
        if (leaf->keys[i] == key) return leaf->vals[i];
    return {};
}

std::vector<RID> BPlusTree::RangeScan(const Value& low, const Value& high) const {
    std::vector<RID> out;
    const Node* leaf = FindLeaf(low);
    while (leaf) {
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] < low) continue;
            if (high < leaf->keys[i]) return out;
            out.insert(out.end(), leaf->vals[i].begin(), leaf->vals[i].end());
        }
        leaf = leaf->next;
    }
    return out;
}

void BPlusTree::Remove(const Value& key, const RID& rid) {
    // Lazy delete: drop the RID (and the key if it becomes empty) from its leaf.
    // No rebalancing; an underfull node still routes and searches correctly.
    Node* n = root_;
    while (!n->leaf) n = n->kids[ChildIndex(n, key)];
    for (size_t i = 0; i < n->keys.size(); ++i) {
        if (n->keys[i] == key) {
            auto& v = n->vals[i];
            v.erase(std::remove(v.begin(), v.end(), rid), v.end());
            if (v.empty()) {
                n->keys.erase(n->keys.begin() + i);
                n->vals.erase(n->vals.begin() + i);
            }
            return;
        }
    }
}

int BPlusTree::HeightOf(const Node* n) {
    int h = 1;
    while (!n->leaf) { n = n->kids[0]; ++h; }
    return h;
}

void BPlusTree::Destroy(Node* n) {
    if (!n) return;
    if (!n->leaf)
        for (Node* k : n->kids) Destroy(k);
    delete n;
}

// ---- Catalog::CreateIndex lives here so it can see the full BPlusTree type ----
IndexInfo* Catalog::CreateIndex(const std::string& table, const std::string& column, bool unique) {
    TableInfo* t = GetTable(table);
    if (!t) return nullptr;
    int col = t->schema.GetColIdx(column);
    if (col < 0) return nullptr;
    if (t->FindIndexOn(col)) return t->FindIndexOn(col);  // already indexed

    IndexInfo ix;
    ix.column = column;
    ix.col_idx = col;
    ix.unique = unique;
    ix.tree = std::make_shared<BPlusTree>();

    // Populate from existing rows: scan the heap, key on the indexed column.
    for (auto it = t->heap->begin(); it != t->heap->end(); ++it) {
        Tuple row = Tuple::Deserialize(it.GetRecord(), t->schema);
        ix.tree->Insert(row.GetValue(col), it.GetRID());
    }
    t->indexes.push_back(std::move(ix));
    return &t->indexes.back();
}

}  // namespace minidb
