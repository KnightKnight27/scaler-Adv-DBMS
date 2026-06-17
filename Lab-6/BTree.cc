// BTree.cc — ADBMS Lab 6, 24BCS10115 Gauri Shukla
//
// Implementation of the B-Tree declared in BTree.h. Insert splits any full
// node on the way down so the leaf that finally takes the key always has a
// free slot (no upward pass). Remove keeps every node it descends into at
// >= t keys by borrowing from a sibling or merging, so deleting one key can
// never push a node below the t-1 floor.

#include "BTree.h"

#include <algorithm>
#include <ostream>
#include <queue>
#include <string>

BTree::BTree(int min_degree) : t_(min_degree < 2 ? 2 : min_degree) {}

BTree::~BTree() { free_subtree(root_); }

void BTree::free_subtree(Node* n) {
    if (!n) return;
    for (Node* c : n->kids) free_subtree(c);
    delete n;
}

// First index i with keys[i] >= key (a plain lower-bound scan within a node).
int BTree::first_ge(const Node* n, int key) const {
    int i = 0;
    const int k = n->num();
    while (i < k && n->keys[i] < key) ++i;
    return i;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

bool BTree::contains(int key) const {
    const Node* cur = root_;
    while (cur) {
        int i = first_ge(cur, key);
        if (i < cur->num() && cur->keys[i] == key) return true;
        if (cur->leaf) return false;
        cur = cur->kids[i];
    }
    return false;
}

// ---------------------------------------------------------------------------
// Insert
// ---------------------------------------------------------------------------

// Split parent->kids[idx], which must hold 2t-1 keys. The median is promoted
// into parent; the upper half becomes a brand-new right sibling.
void BTree::split_child(Node* parent, int idx) {
    Node* y   = parent->kids[idx];
    Node* z   = new Node(y->leaf);
    const int mid = t_ - 1;          // median position inside y
    int median = y->keys[mid];

    // z takes the t-1 keys above the median.
    z->keys.assign(y->keys.begin() + mid + 1, y->keys.end());
    if (!y->leaf)
        z->kids.assign(y->kids.begin() + t_, y->kids.end());

    // y keeps the t-1 keys below the median.
    y->keys.resize(mid);
    if (!y->leaf) y->kids.resize(t_);

    parent->keys.insert(parent->keys.begin() + idx, median);
    parent->kids.insert(parent->kids.begin() + idx + 1, z);
}

void BTree::insert(int key) {
    if (contains(key)) return;                 // set semantics: ignore duplicates

    if (!root_) {
        root_ = new Node(true);
        root_->keys.push_back(key);
        ++count_;
        return;
    }
    if (root_->full(t_)) {                      // grow height by one level
        Node* fresh = new Node(false);
        fresh->kids.push_back(root_);
        root_ = fresh;
        split_child(fresh, 0);
    }
    insert_nonfull(root_, key);
    ++count_;
}

void BTree::insert_nonfull(Node* n, int key) {
    int i = first_ge(n, key);
    if (n->leaf) {
        n->keys.insert(n->keys.begin() + i, key);
        return;
    }
    if (n->kids[i]->full(t_)) {
        split_child(n, i);
        if (key > n->keys[i]) ++i;             // median rose up; pick the right side
    }
    insert_nonfull(n->kids[i], key);
}

// ---------------------------------------------------------------------------
// Remove
// ---------------------------------------------------------------------------

int BTree::rightmost_key(Node* n) const {
    while (!n->leaf) n = n->kids.back();
    return n->keys.back();
}

int BTree::leftmost_key(Node* n) const {
    while (!n->leaf) n = n->kids.front();
    return n->keys.front();
}

// Borrow one key from the left sibling, rotating it through the parent.
void BTree::rotate_from_left(Node* parent, int idx) {
    Node* child = parent->kids[idx];
    Node* sib   = parent->kids[idx - 1];

    child->keys.insert(child->keys.begin(), parent->keys[idx - 1]);
    parent->keys[idx - 1] = sib->keys.back();
    sib->keys.pop_back();

    if (!child->leaf) {
        child->kids.insert(child->kids.begin(), sib->kids.back());
        sib->kids.pop_back();
    }
}

// Borrow one key from the right sibling, rotating it through the parent.
void BTree::rotate_from_right(Node* parent, int idx) {
    Node* child = parent->kids[idx];
    Node* sib   = parent->kids[idx + 1];

    child->keys.push_back(parent->keys[idx]);
    parent->keys[idx] = sib->keys.front();
    sib->keys.erase(sib->keys.begin());

    if (!child->leaf) {
        child->kids.push_back(sib->kids.front());
        sib->kids.erase(sib->kids.begin());
    }
}

// Fuse kids[idx] and kids[idx+1] into one node, pulling keys[idx] down as the
// separator between them. The result holds exactly 2t-1 keys.
void BTree::merge_children(Node* parent, int idx) {
    Node* left  = parent->kids[idx];
    Node* right = parent->kids[idx + 1];

    left->keys.push_back(parent->keys[idx]);
    left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
    if (!left->leaf)
        left->kids.insert(left->kids.end(), right->kids.begin(), right->kids.end());

    parent->keys.erase(parent->keys.begin() + idx);
    parent->kids.erase(parent->kids.begin() + idx + 1);
    delete right;                               // its children now belong to left
}

// Guarantee parent->kids[idx] has at least t keys before we descend into it.
void BTree::ensure_fat(Node* parent, int idx) {
    if (parent->kids[idx]->num() >= t_) return;

    bool left_ok  = idx > 0              && parent->kids[idx - 1]->num() >= t_;
    bool right_ok = idx < parent->num()  && parent->kids[idx + 1]->num() >= t_;

    if (left_ok)        rotate_from_left(parent, idx);
    else if (right_ok)  rotate_from_right(parent, idx);
    else if (idx < parent->num()) merge_children(parent, idx);      // merge with right
    else                          merge_children(parent, idx - 1);  // merge with left
}

bool BTree::remove_from(Node* n, int key) {
    int i = first_ge(n, key);

    if (i < n->num() && n->keys[i] == key) {            // key found in this node
        if (n->leaf) {
            n->keys.erase(n->keys.begin() + i);         // case 1: just drop it
            return true;
        }
        // Internal node: replace with predecessor / successor, or merge.
        if (n->kids[i]->num() >= t_) {                  // case 2a
            int pred = rightmost_key(n->kids[i]);
            n->keys[i] = pred;
            return remove_from(n->kids[i], pred);
        }
        if (n->kids[i + 1]->num() >= t_) {              // case 2b
            int succ = leftmost_key(n->kids[i + 1]);
            n->keys[i] = succ;
            return remove_from(n->kids[i + 1], succ);
        }
        merge_children(n, i);                           // case 2c: both minimal
        return remove_from(n->kids[i], key);
    }

    if (n->leaf) return false;                          // key simply not present

    // Descend. Make sure the child we enter is fat enough first.
    bool last = (i == n->num());
    if (n->kids[i]->num() < t_) ensure_fat(n, i);
    // A merge may have collapsed kids[i] and kids[i+1]; if we were aiming past
    // the last separator, the target now lives one slot to the left.
    if (last && i > n->num()) --i;
    return remove_from(n->kids[i], key);
}

bool BTree::remove(int key) {
    if (!root_) return false;
    bool removed = remove_from(root_, key);

    if (root_->keys.empty()) {                          // root emptied out
        Node* old = root_;
        root_ = root_->leaf ? nullptr : root_->kids[0];
        old->kids.clear();                              // don't free the new root
        delete old;
    }
    if (removed) --count_;
    return removed;
}

// ---------------------------------------------------------------------------
// Read-only walks
// ---------------------------------------------------------------------------

int BTree::height_of(const Node* n) const {
    if (!n) return 0;
    return n->leaf ? 1 : 1 + height_of(n->kids[0]);
}

int BTree::height() const { return height_of(root_); }

void BTree::collect_inorder(const Node* n, std::vector<int>& out) const {
    const int k = n->num();
    for (int i = 0; i < k; ++i) {
        if (!n->leaf) collect_inorder(n->kids[i], out);
        out.push_back(n->keys[i]);
    }
    if (!n->leaf) collect_inorder(n->kids[k], out);
}

std::vector<int> BTree::inorder() const {
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(count_));
    if (root_) collect_inorder(root_, out);
    return out;
}

// Level-order picture: one line per depth, nodes shown as {k1 k2 ...} and
// separated by " | ". Same BFS flavour as the Red-Black Tree print in Lab 5.
void BTree::print(std::ostream& os) const {
    if (!root_) { os << "<empty tree>\n"; return; }

    std::queue<std::pair<Node*, int>> q;
    q.push({root_, 0});
    int cur_level = 0;
    os << "L0: ";
    while (!q.empty()) {
        auto [n, lvl] = q.front();
        q.pop();
        if (lvl != cur_level) {
            cur_level = lvl;
            os << "\nL" << lvl << ": ";
        }
        os << "{";
        for (int i = 0; i < n->num(); ++i) {
            if (i) os << ' ';
            os << n->keys[i];
        }
        os << "} ";
        if (!n->leaf)
            for (Node* c : n->kids) q.push({c, lvl + 1});
    }
    os << "\n";
}

// ---------------------------------------------------------------------------
// Invariant checker
// ---------------------------------------------------------------------------

std::string BTree::check(const Node* n, bool is_root, int depth, int& leaf_depth,
                         bool have_lo, int lo, bool have_hi, int hi) const {
    const int k = n->num();
    if (k > 2 * t_ - 1)              return "node exceeds 2t-1 keys";
    if (!is_root && k < t_ - 1)      return "non-root node below t-1 keys";
    if (is_root && k < 1)            return "root has no keys but tree is non-empty";

    for (int i = 0; i < k; ++i) {
        if (i > 0 && n->keys[i - 1] >= n->keys[i])
            return "keys not strictly increasing within a node";
        if (have_lo && n->keys[i] <= lo) return "key out of range (<= lower bound)";
        if (have_hi && n->keys[i] >= hi) return "key out of range (>= upper bound)";
    }

    if (n->leaf) {
        if (!n->kids.empty()) return "leaf carries children";
        if (leaf_depth == -1) leaf_depth = depth;
        else if (leaf_depth != depth) return "leaves are at differing depths";
        return "";
    }

    if (n->num() + 1 != static_cast<int>(n->kids.size()))
        return "internal node child count != keys + 1";

    for (int i = 0; i <= k; ++i) {
        bool c_lo = (i > 0)  || have_lo;
        int  v_lo = (i > 0)  ? n->keys[i - 1] : lo;
        bool c_hi = (i < k)  || have_hi;
        int  v_hi = (i < k)  ? n->keys[i]     : hi;
        std::string e = check(n->kids[i], false, depth + 1, leaf_depth,
                              c_lo, v_lo, c_hi, v_hi);
        if (!e.empty()) return e;
    }
    return "";
}

std::string BTree::validate() const {
    if (!root_) return "";
    int leaf_depth = -1;
    return check(root_, true, 0, leaf_depth, false, 0, false, 0);
}
