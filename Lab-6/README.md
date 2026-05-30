# Lab 6 — B-Tree (CLRS chapter 18)

A single-file templated B-tree over `(Key, Value)` pairs with a custom
comparator. Every B-tree node is sized to hold up to `2t-1` keys and
`2t` children, where `t` is the **minimum degree** — in a real database
each node corresponds to one disk page (SQLite = 4 KB, PostgreSQL =
8 KB), which is why databases prefer B-trees over binary trees for
on-disk indexes.

## Layout

```
Lab-6/
├── btree.cpp     # everything: BTree<K,V,Cmp> + main() with six demo scenarios
├── btree         # compiled binary (built locally; not checked in)
└── README.md
```

## The five invariants

After every operation the tree satisfies:

1. **Key count.** Every non-root node holds between `t-1` and `2t-1`
   keys. The root holds at least 1 (or 0 if the tree is empty).
2. **Upper bound.** No node ever holds more than `2t-1` keys.
3. **Fanout.** An internal node with `k` keys has exactly `k+1` children.
4. **Sorted within a node.** Keys inside a node are in strictly
   ascending order by `Compare`.
5. **Uniform depth.** All leaves sit at the same depth from the root.

`BTree::verify()` walks the tree recursively and asserts every one of
these. The demo calls it after each scenario.

## Public API

```cpp
template <typename Key, typename Value, typename Compare = std::less<Key>>
class BTree {
public:
    explicit BTree(int min_degree = 32, Compare cmp = Compare{});

    bool                 contains(const Key& k) const;
    std::optional<Value> at(const Key& k) const;
    std::size_t          size() const;

    void                 insert(const Key& k, const Value& v);   // overwrites on duplicate
    bool                 erase(const Key& k);

    std::vector<std::pair<Key, Value>> inorder() const;
    void                 print() const;     // level-order, bracketed keys per node
    int                  verify() const;    // asserts invariants; returns leaf depth
};
```

## Algorithms

### Insert — proactive split on the way down

```
insert(root, k, v):
    if root is full (2t-1 keys):
        grow the tree: new root with old root as first child, then splitChild(new_root, 0)
    insertNonFull(root, k, v)
```

`insertNonFull` walks toward a leaf. Whenever it is about to step into
a child that is already full, it **splits that child first**. The
median moves up into the current node; the two halves each have `t-1`
keys. After a split the parent has room for the new key, so upward
propagation never needs more than one level of revisit.

Overwrite: when `insertNonFull` finds the key already present, it
replaces the value in place; `size_` is not bumped.

### Erase — six CLRS cases

`eraseFrom(n, k)` keeps the invariant that whenever we descend, the
child we step into has at least `t` keys. That's `ensureChildHasEnough`:

- **Borrow from left sibling** (sibling has ≥ `t` keys): rotate the
  parent's separator down into the child and the sibling's last key up.
- **Borrow from right sibling** (mirror).
- **Merge** with a sibling: pulls the parent separator down between the
  two children, concatenating everything into one node with
  `(t-1) + 1 + (t-1) = 2t-1` keys.

With that guarantee, the six deletion cases are:

| Case | When                                                            | Action |
| ---- | --------------------------------------------------------------- | ------ |
| 1    | `k` is in a leaf                                                | remove from the leaf |
| 2a   | `k` is in internal node; left child has ≥ `t` keys              | swap with **predecessor**, recurse left |
| 2b   | `k` is in internal node; right child has ≥ `t` keys             | swap with **successor**, recurse right |
| 2c   | `k` is in internal node; both adjacent children have `t-1` keys | merge them around `k`, recurse into the merged node |
| 3a   | `k` not here; target child has < `t` keys; a sibling has ≥ `t`  | borrow from that sibling, then descend |
| 3b   | `k` not here; target child and all siblings have `t-1` keys     | merge with a sibling, then descend |

After the recursion, if the root ends up with 0 keys but one child,
the tree drops one level (`root_ = root_->children[0]`).

### Search

Standard B-tree descent with binary search inside each node. Cost is
`O(log_t n)` node touches, each doing `O(log t)` comparisons — total
`O(log n)`.

## Complexity

| Operation         | Time     | Notes                                |
| ----------------- | -------- | ------------------------------------ |
| `contains` / `at` | O(log n) | binary search per level              |
| `insert`          | O(log n) | at most one split per level          |
| `erase`           | O(log n) | at most one borrow/merge per level   |
| `inorder`         | O(n)     | yields keys in sorted order          |
| `verify`          | O(n)     | one DFS, asserts every invariant     |

Height is bounded by `log_t((n + 1) / 2)`. With `t = 32` even a
billion-entry tree is only ~6 levels deep — the whole point of a
B-tree is touching fewer pages per query.

## Build & run

```bash
cd Lab-6
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 btree.cpp -o btree
./btree
```

The binary is not checked in — `.gitignore` excludes `Lab-6/btree` and
`Lab-6/btree.exe`. If you don't have g++ on PATH any C++17 compiler
works (`clang++`, MSVC `cl /std:c++17 btree.cpp`).

## Demo scenarios

`main()` runs six tests; each calls `verify()` so any invariant
violation aborts immediately:

1. **Sequential insert** of `1..50` with `t=3`. Confirms in-order
   traversal returns sorted keys and reports the leaf depth.
2. **Reverse-order insert** of `50..1` — exercises the opposite split
   pattern.
3. **Random insert** of 200 keys (range 0–999) with `t=4`, compared
   against a `std::map` oracle.
4. **Overwrite semantics**: re-inserting an existing key updates the
   value without growing `size()`.
5. **Deletion cases**: builds a multi-level tree, then deletes leaf
   keys, internal keys, and forces merges to cover every CLRS case.
6. **5,000-op stress test** mixing insert (60%), erase (30%), and
   lookup (10%) against `std::map`. Compares `contains` live and walks
   the entire `inorder` traversal at the end.

Expected output ends with `All B-tree scenarios passed.`

## Why this matches a real database

Real DBs typically use **B+ trees** where only leaves hold values and
leaves are linked for range scans — but the splitting / merging /
borrowing machinery is identical, and the per-node key bounds are the
same. The biggest difference vs the toy version here is that real
nodes are serialised to fixed-size disk pages and a buffer pool (see
Lab 3) decides which nodes live in memory.
