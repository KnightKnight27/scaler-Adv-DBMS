// Lab 6 - B-Tree (CLRS chapter 18)
//
// Single-file templated B-tree over (Key, Value) pairs with a custom
// comparator. Every B-tree node is sized to hold up to 2t-1 keys and
// 2t children, where `t` is the minimum degree. In a real database
// each node corresponds to one disk page (SQLite = 4 KB, PostgreSQL =
// 8 KB); here nodes live in memory, but the structure is identical.
//
// Invariants maintained after every operation:
//
//   1. Every non-root node has at least t-1 keys; the root has >= 1
//      (or 0 if the tree is empty).
//   2. Every node has at most 2t-1 keys.
//   3. An internal node with k keys has exactly k+1 children.
//   4. Keys inside a node are sorted by Compare.
//   5. All leaves are at the same depth.
//
// Insertion uses proactive splitting: while descending, any full child
// (2t-1 keys) is split before we step into it. Deletion uses the
// six-case CLRS algorithm (borrow-left, borrow-right, merge,
// predecessor swap, successor swap, merge-around-key).
//
// Build:
//   g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 btree.cpp -o btree
//   ./btree

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

template <typename Key, typename Value, typename Compare = std::less<Key>>
class BTree {
public:
    explicit BTree(int min_degree = 32, Compare cmp = Compare{})
        : t_(min_degree), cmp_(std::move(cmp)), root_(std::make_unique<Node>(true)) {
        assert(t_ >= 2 && "B-tree minimum degree must be >= 2");
    }

    BTree(const BTree&)            = delete;
    BTree& operator=(const BTree&) = delete;

    bool        contains(const Key& k) const { return search(root_.get(), k) != nullptr; }
    std::size_t size() const { return size_; }
    bool        empty() const { return size_ == 0; }
    int         minDegree() const { return t_; }

    std::optional<Value> at(const Key& k) const {
        const Node* n = search(root_.get(), k);
        if (n == nullptr) return std::nullopt;
        int i = lowerBoundIndex(*n, k);
        return n->entries[i].second;
    }

    // Inserts (k, v); overwrites the value if k already exists.
    void insert(const Key& k, const Value& v) {
        Node* root = root_.get();
        if (isFull(root)) {
            auto new_root = std::make_unique<Node>(false);
            new_root->children.push_back(std::move(root_));
            splitChild(new_root.get(), 0);
            root_ = std::move(new_root);
        }
        insertNonFull(root_.get(), k, v);
    }

    // Erases k. Returns true if a key was removed.
    bool erase(const Key& k) {
        bool removed = eraseFrom(root_.get(), k);
        if (root_->entries.empty() && !root_->is_leaf) {
            root_ = std::move(root_->children[0]);
        }
        if (removed) --size_;
        return removed;
    }

    std::vector<std::pair<Key, Value>> inorder() const {
        std::vector<std::pair<Key, Value>> out;
        out.reserve(size_);
        inorderInto(root_.get(), out);
        return out;
    }

    // Level-order print for debugging.
    void print() const {
        if (root_->entries.empty() && root_->is_leaf) {
            std::cout << "(empty tree)\n";
            return;
        }
        std::queue<std::pair<const Node*, int>> q;
        q.push({root_.get(), 0});
        int depth = -1;
        while (!q.empty()) {
            auto [n, d] = q.front();
            q.pop();
            if (d != depth) {
                if (depth != -1) std::cout << '\n';
                std::cout << "L" << d << ":";
                depth = d;
            }
            std::cout << " [";
            for (std::size_t i = 0; i < n->entries.size(); ++i) {
                std::cout << n->entries[i].first
                          << (i + 1 == n->entries.size() ? "" : ",");
            }
            std::cout << "]";
            if (!n->is_leaf) {
                for (const auto& c : n->children) q.push({c.get(), d + 1});
            }
        }
        std::cout << '\n';
    }

    // Walks the tree and asserts every invariant. Returns leaf depth.
    int verify() const {
        if (root_->entries.empty() && root_->is_leaf) return 0;
        int leaf_depth = -1;
        verifyNode(root_.get(), 0, true, leaf_depth);
        return leaf_depth;
    }

private:
    struct Node {
        bool                                is_leaf;
        std::vector<std::pair<Key, Value>>  entries;   // size: k
        std::vector<std::unique_ptr<Node>>  children;  // size: k+1, empty if leaf
        explicit Node(bool leaf) : is_leaf(leaf) {}
    };

    int                   t_;
    Compare               cmp_;
    std::unique_ptr<Node> root_;
    std::size_t           size_ = 0;

    bool keyEqual(const Key& a, const Key& b) const { return !cmp_(a, b) && !cmp_(b, a); }
    bool isFull(const Node* n) const { return static_cast<int>(n->entries.size()) == 2 * t_ - 1; }

    int lowerBoundIndex(const Node& n, const Key& k) const {
        int lo = 0, hi = static_cast<int>(n.entries.size());
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (cmp_(n.entries[mid].first, k)) lo = mid + 1;
            else                               hi = mid;
        }
        return lo;
    }

    const Node* search(const Node* n, const Key& k) const {
        int i = lowerBoundIndex(*n, k);
        if (i < static_cast<int>(n->entries.size()) && keyEqual(n->entries[i].first, k))
            return n;
        if (n->is_leaf) return nullptr;
        return search(n->children[i].get(), k);
    }

    // Split children[idx] of `parent` (must be full). Median moves up.
    void splitChild(Node* parent, int idx) {
        Node* full = parent->children[idx].get();
        assert(isFull(full));

        auto right = std::make_unique<Node>(full->is_leaf);
        right->entries.assign(
            std::make_move_iterator(full->entries.begin() + t_),
            std::make_move_iterator(full->entries.end()));

        if (!full->is_leaf) {
            right->children.assign(
                std::make_move_iterator(full->children.begin() + t_),
                std::make_move_iterator(full->children.end()));
            full->children.resize(t_);
        }

        auto median = std::move(full->entries[t_ - 1]);
        full->entries.resize(t_ - 1);

        parent->entries.insert(parent->entries.begin() + idx, std::move(median));
        parent->children.insert(parent->children.begin() + idx + 1, std::move(right));
    }

    void insertNonFull(Node* n, const Key& k, const Value& v) {
        int i = lowerBoundIndex(*n, k);

        if (i < static_cast<int>(n->entries.size()) && keyEqual(n->entries[i].first, k)) {
            n->entries[i].second = v;           // overwrite
            return;
        }

        if (n->is_leaf) {
            n->entries.insert(n->entries.begin() + i, {k, v});
            ++size_;
            return;
        }

        if (isFull(n->children[i].get())) {
            splitChild(n, i);
            if (cmp_(n->entries[i].first, k)) ++i;
            else if (keyEqual(n->entries[i].first, k)) {
                n->entries[i].second = v;
                return;
            }
        }
        insertNonFull(n->children[i].get(), k, v);
    }

    // In-order predecessor / successor of a key sitting in node n.
    // Pure reads -- they do not remove anything; the caller schedules
    // a recursive erase so the size accounting stays in one place.
    std::pair<Key, Value> predecessor(const Node* n) const {
        const Node* cur = n;
        while (!cur->is_leaf) cur = cur->children.back().get();
        return cur->entries.back();
    }

    std::pair<Key, Value> successor(const Node* n) const {
        const Node* cur = n;
        while (!cur->is_leaf) cur = cur->children.front().get();
        return cur->entries.front();
    }

    // Ensures children[idx] has at least t entries before we descend.
    // Either borrows from a sibling (case 3a) or merges (case 3b).
    void ensureChildHasEnough(Node* parent, int idx) {
        Node* child = parent->children[idx].get();
        if (static_cast<int>(child->entries.size()) >= t_) return;

        Node* left  = (idx > 0) ? parent->children[idx - 1].get() : nullptr;
        Node* right = (idx + 1 < static_cast<int>(parent->children.size()))
                          ? parent->children[idx + 1].get() : nullptr;

        if (left && static_cast<int>(left->entries.size()) >= t_) {
            child->entries.insert(child->entries.begin(),
                                  std::move(parent->entries[idx - 1]));
            if (!child->is_leaf) {
                child->children.insert(child->children.begin(),
                                       std::move(left->children.back()));
                left->children.pop_back();
            }
            parent->entries[idx - 1] = std::move(left->entries.back());
            left->entries.pop_back();
            return;
        }
        if (right && static_cast<int>(right->entries.size()) >= t_) {
            child->entries.push_back(std::move(parent->entries[idx]));
            if (!child->is_leaf) {
                child->children.push_back(std::move(right->children.front()));
                right->children.erase(right->children.begin());
            }
            parent->entries[idx] = std::move(right->entries.front());
            right->entries.erase(right->entries.begin());
            return;
        }
        if (left) mergeChildren(parent, idx - 1);
        else      mergeChildren(parent, idx);
    }

    void mergeChildren(Node* parent, int idx) {
        Node* left  = parent->children[idx].get();
        auto  right = std::move(parent->children[idx + 1]);

        left->entries.push_back(std::move(parent->entries[idx]));
        parent->entries.erase(parent->entries.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);

        left->entries.insert(left->entries.end(),
                             std::make_move_iterator(right->entries.begin()),
                             std::make_move_iterator(right->entries.end()));
        if (!left->is_leaf) {
            left->children.insert(left->children.end(),
                                  std::make_move_iterator(right->children.begin()),
                                  std::make_move_iterator(right->children.end()));
        }
    }

    bool eraseFrom(Node* n, const Key& k) {
        int i = lowerBoundIndex(*n, k);
        bool here = (i < static_cast<int>(n->entries.size()) &&
                     keyEqual(n->entries[i].first, k));

        if (here && n->is_leaf) {                       // case 1
            n->entries.erase(n->entries.begin() + i);
            return true;
        }

        if (here) {
            Node* leftChild  = n->children[i].get();
            Node* rightChild = n->children[i + 1].get();
            if (static_cast<int>(leftChild->entries.size()) >= t_) {
                auto pred = predecessor(leftChild);     // case 2a
                Key pred_key = pred.first;
                n->entries[i] = std::move(pred);
                return eraseFrom(leftChild, pred_key);
            }
            if (static_cast<int>(rightChild->entries.size()) >= t_) {
                auto succ = successor(rightChild);      // case 2b
                Key succ_key = succ.first;
                n->entries[i] = std::move(succ);
                return eraseFrom(rightChild, succ_key);
            }
            mergeChildren(n, i);                        // case 2c
            return eraseFrom(n->children[i].get(), k);
        }

        if (n->is_leaf) return false;                    // key not in tree

        // case 3: descend.
        bool last = (i == static_cast<int>(n->entries.size()));
        ensureChildHasEnough(n, i);
        // If we descended into the rightmost child and a merge with the
        // left sibling happened, the target lives at i-1 now.
        if (last && i > static_cast<int>(n->entries.size())) --i;
        return eraseFrom(n->children[i].get(), k);
    }

    void inorderInto(const Node* n, std::vector<std::pair<Key, Value>>& out) const {
        if (n->is_leaf) {
            for (const auto& e : n->entries) out.push_back(e);
            return;
        }
        for (std::size_t i = 0; i < n->entries.size(); ++i) {
            inorderInto(n->children[i].get(), out);
            out.push_back(n->entries[i]);
        }
        inorderInto(n->children.back().get(), out);
    }

    void verifyNode(const Node* n, int depth, bool is_root, int& leaf_depth) const {
        int k = static_cast<int>(n->entries.size());
        if (!is_root) assert(k >= t_ - 1 && "B-tree: non-root has at least t-1 keys");
        assert(k <= 2 * t_ - 1 && "B-tree: at most 2t-1 keys");

        for (int i = 1; i < k; ++i) {
            assert(cmp_(n->entries[i - 1].first, n->entries[i].first) &&
                   "B-tree: keys must be sorted within a node");
        }

        if (n->is_leaf) {
            if (leaf_depth == -1) leaf_depth = depth;
            assert(depth == leaf_depth && "B-tree: all leaves at the same depth");
            return;
        }

        assert(static_cast<int>(n->children.size()) == k + 1 &&
               "B-tree: internal node has k+1 children");

        for (int i = 0; i <= k; ++i) {
            const Node* child = n->children[i].get();
            if (i > 0 && !child->entries.empty()) {
                assert(cmp_(n->entries[i - 1].first, child->entries.front().first) &&
                       "B-tree: child keys must be > left separator");
            }
            if (i < k && !child->entries.empty()) {
                assert(cmp_(child->entries.back().first, n->entries[i].first) &&
                       "B-tree: child keys must be < right separator");
            }
            verifyNode(child, depth + 1, false, leaf_depth);
        }
    }
};

// ---------------------------------------------------------------------
// Demo driver. Six scenarios; each calls verify() so any invariant
// violation aborts immediately.

namespace {

void requireEqual(const std::vector<std::pair<int, int>>& got,
                  const std::map<int, int>& expected,
                  const std::string& label) {
    if (got.size() != expected.size()) {
        std::cerr << label << ": size mismatch got=" << got.size()
                  << " expected=" << expected.size() << "\n";
        std::abort();
    }
    auto it = expected.begin();
    for (const auto& [k, v] : got) {
        if (k != it->first || v != it->second) {
            std::cerr << label << ": entry mismatch at key " << k << "\n";
            std::abort();
        }
        ++it;
    }
}

void scenario1_sequential() {
    std::cout << "[1] sequential insert 1..50 (t=3)\n";
    BTree<int, int> tree(3);
    for (int i = 1; i <= 50; ++i) tree.insert(i, i * 10);
    int depth = tree.verify();
    auto v = tree.inorder();
    assert(v.size() == 50);
    for (int i = 0; i < 50; ++i) {
        assert(v[i].first == i + 1 && v[i].second == (i + 1) * 10);
    }
    std::cout << "    size=" << tree.size() << "  depth=" << depth << "  ok\n";
}

void scenario2_reverse() {
    std::cout << "[2] reverse-order insert 50..1\n";
    BTree<int, int> tree(3);
    for (int i = 50; i >= 1; --i) tree.insert(i, i);
    tree.verify();
    auto v = tree.inorder();
    for (int i = 0; i < 50; ++i) assert(v[i].first == i + 1);
    std::cout << "    ok\n";
}

void scenario3_random() {
    std::cout << "[3] random insert 200 keys (t=4)\n";
    std::mt19937 rng(42);
    BTree<int, int> tree(4);
    std::map<int, int> oracle;
    for (int i = 0; i < 200; ++i) {
        int k = static_cast<int>(rng() % 1000);
        int v = static_cast<int>(rng() % 1000);
        tree.insert(k, v);
        oracle[k] = v;
    }
    tree.verify();
    requireEqual(tree.inorder(), oracle, "scenario3");
    std::cout << "    size=" << tree.size() << "  ok\n";
}

void scenario4_overwrite() {
    std::cout << "[4] overwrite semantics\n";
    BTree<std::string, int> tree(3);
    tree.insert("alpha", 1);
    tree.insert("beta", 2);
    tree.insert("alpha", 100);
    assert(tree.size() == 2);
    assert(tree.at("alpha").value() == 100);
    assert(tree.at("beta").value() == 2);
    assert(!tree.at("gamma").has_value());
    tree.verify();
    std::cout << "    ok\n";
}

void scenario5_deletion_cases() {
    std::cout << "[5] deletion - every CLRS case\n";
    BTree<int, int> tree(3);
    for (int k : {10, 20, 30, 40, 50, 60, 70, 5, 15, 25, 35, 45, 55, 65, 75,
                  1, 8, 12, 18, 22, 28}) {
        tree.insert(k, k);
    }
    tree.verify();

    assert(tree.erase(1));                                 // case 1: leaf
    tree.verify();
    for (int k : {30, 20, 50, 70, 10}) {                   // case 2 + 3 mixed
        assert(tree.erase(k));
        assert(!tree.contains(k));
        tree.verify();
    }
    std::size_t before = tree.size();
    assert(!tree.erase(999));
    assert(tree.size() == before);

    std::cout << "    size=" << tree.size() << "  ok\n";
}

void scenario6_stress() {
    std::cout << "[6] 5,000-op stress vs std::map (t=3)\n";
    std::mt19937 rng(12345);
    BTree<int, int>    tree(3);
    std::map<int, int> oracle;

    constexpr int kOps   = 5000;
    constexpr int kRange = 500;

    for (int op = 0; op < kOps; ++op) {
        int k    = static_cast<int>(rng() % kRange);
        int dice = static_cast<int>(rng() % 100);
        if (dice < 60) {
            int v = static_cast<int>(rng() % 100000);
            tree.insert(k, v);
            oracle[k] = v;
        } else if (dice < 90) {
            bool tree_had   = tree.contains(k);
            bool oracle_had = oracle.count(k) != 0;
            assert(tree_had == oracle_had);
            tree.erase(k);
            oracle.erase(k);
        } else {
            auto v = tree.at(k);
            if (oracle.count(k)) assert(v.has_value() && v.value() == oracle[k]);
            else                 assert(!v.has_value());
        }
        if (op % 500 == 0) tree.verify();
    }

    tree.verify();
    requireEqual(tree.inorder(), oracle, "scenario6");
    std::cout << "    final size=" << tree.size() << "  ok\n";
}

}  // namespace

int main() {
    scenario1_sequential();
    scenario2_reverse();
    scenario3_random();
    scenario4_overwrite();
    scenario5_deletion_cases();
    scenario6_stress();
    std::cout << "\nAll B-tree scenarios passed.\n";
    return 0;
}
