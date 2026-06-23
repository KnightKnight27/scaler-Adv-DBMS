#include "index/bplus_tree.h"

namespace minidb {

static constexpr int MAX_KEYS = BTREE_ORDER - 1; // 3

// ─── Helper: find first position where keys[pos] >= key ─────────────────────
static int LB(const std::vector<int32_t>& keys, int32_t key) {
    int lo = 0, hi = (int)keys.size();
    while (lo < hi) { int m=(lo+hi)/2; if(keys[m]<key) lo=m+1; else hi=m; }
    return lo;
}

// ─── Ctor/Dtor ───────────────────────────────────────────────────────────────
BPlusTree::BPlusTree() { root_ = new BPNode(true); }
BPlusTree::~BPlusTree() { FreeTree(root_); }
void BPlusTree::FreeTree(BPNode* n) {
    if (!n) return;
    if (!n->is_leaf) for (auto* c : n->children) FreeTree(c);
    delete n;
}

// ─── FindLeaf ────────────────────────────────────────────────────────────────
BPNode* BPlusTree::FindLeaf(int32_t key) const {
    BPNode* cur = root_;
    while (!cur->is_leaf) {
        int i = (int)cur->keys.size();
        for (int j=0; j<(int)cur->keys.size(); ++j)
            if (key < cur->keys[j]) { i=j; break; }
        cur = cur->children[i];
    }
    return cur;
}

// ─── Search ──────────────────────────────────────────────────────────────────
bool BPlusTree::Search(int32_t key, page_id_t* out) const {
    BPNode* leaf = FindLeaf(key);
    for (int i=0; i<(int)leaf->keys.size(); ++i)
        if (leaf->keys[i]==key) { if(out) *out=leaf->values[i]; return true; }
    return false;
}

// ─── SplitLeaf ───────────────────────────────────────────────────────────────
BPNode* BPlusTree::SplitLeaf(BPNode* leaf, int32_t* up_key) {
    int mid = (int)leaf->keys.size() / 2;
    BPNode* right = new BPNode(true);
    right->keys  .assign(leaf->keys.begin()+mid,   leaf->keys.end());
    right->values.assign(leaf->values.begin()+mid, leaf->values.end());
    leaf->keys  .erase(leaf->keys.begin()+mid,   leaf->keys.end());
    leaf->values.erase(leaf->values.begin()+mid, leaf->values.end());
    right->next = leaf->next; leaf->next = right;
    *up_key = right->keys[0];
    return right;
}

// ─── SplitInternal ───────────────────────────────────────────────────────────
BPNode* BPlusTree::SplitInternal(BPNode* node, int32_t* up_key) {
    int mid = (int)node->keys.size() / 2;
    *up_key = node->keys[mid];
    BPNode* right = new BPNode(false);
    right->keys    .assign(node->keys.begin()+mid+1,      node->keys.end());
    right->children.assign(node->children.begin()+mid+1,  node->children.end());
    node->keys    .erase(node->keys.begin()+mid,     node->keys.end());
    node->children.erase(node->children.begin()+mid+1, node->children.end());
    return right;
}

// ─── InsertRec (recursive) ───────────────────────────────────────────────────
BPNode* BPlusTree::InsertRec(BPNode* node, int32_t key, page_id_t val, int32_t* up_key) {
    if (node->is_leaf) {
        int pos = LB(node->keys, key);
        if (pos < (int)node->keys.size() && node->keys[pos]==key) return nullptr; // dup
        node->keys  .insert(node->keys.begin()+pos,   key);
        node->values.insert(node->values.begin()+pos, val);
        if ((int)node->keys.size() <= MAX_KEYS) return nullptr;
        return SplitLeaf(node, up_key);
    }
    // Internal node: recurse.
    int pos = (int)node->keys.size();
    for (int j=0; j<(int)node->keys.size(); ++j) if (key<node->keys[j]) { pos=j; break; }
    int32_t child_up;
    BPNode* sibling = InsertRec(node->children[pos], key, val, &child_up);
    if (!sibling) return nullptr;
    node->keys    .insert(node->keys.begin()+pos,      child_up);
    node->children.insert(node->children.begin()+pos+1, sibling);
    if ((int)node->keys.size() <= MAX_KEYS) return nullptr;
    return SplitInternal(node, up_key);
}

// ─── Insert ──────────────────────────────────────────────────────────────────
void BPlusTree::Insert(int32_t key, page_id_t val) {
    int32_t up_key;
    BPNode* sibling = InsertRec(root_, key, val, &up_key);
    if (sibling) {
        BPNode* new_root = new BPNode(false);
        new_root->keys.push_back(up_key);
        new_root->children.push_back(root_);
        new_root->children.push_back(sibling);
        root_ = new_root;
    }
}

// ─── Delete (lazy leaf removal) ──────────────────────────────────────────────
void BPlusTree::Delete(int32_t key) {
    BPNode* leaf = FindLeaf(key);
    for (int i=0; i<(int)leaf->keys.size(); ++i)
        if (leaf->keys[i]==key) {
            leaf->keys  .erase(leaf->keys.begin()+i);
            leaf->values.erase(leaf->values.begin()+i);
            return;
        }
}
 
std::vector<std::pair<int32_t, page_id_t>> BPlusTree::ScanRange(int32_t key_low) const {
    std::vector<std::pair<int32_t, page_id_t>> res;
    BPNode* leaf = FindLeaf(key_low);
    while (leaf) {
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] > key_low) {
                res.push_back({leaf->keys[i], leaf->values[i]});
            }
        }
        leaf = leaf->next;
    }
    return res;
}

} // namespace minidb
