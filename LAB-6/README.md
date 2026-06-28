# Lab 6 — B-Tree

> **Course:** Advanced DBMS
> **Author:** Bhavya Jain
> **Roll No:** 23BCS10088
> **Email:** Bhavya.23bcs10088@sst.scaler.com
> **Language:** C++17

A full B-Tree of minimum degree `t` (every non-root node holds between
`t-1` and `2t-1` keys). Supports `insert`, `erase`, and `search`, with an
interactive menu driver and a randomized test suite. This is the
in-memory analogue of the on-disk index every production DBMS leans on
(PostgreSQL `nbtree`, MySQL InnoDB, SQLite).

## Files

| File | Purpose |
| --- | --- |
| `btree.h`   | `TreeNode` + `BalancedTree` declarations |
| `btree.cpp` | Insert (split-on-the-way-down), erase (borrow/merge), search, traversals |
| `main.cpp`  | Interactive REPL — `make run` |
| `test.cpp`  | Directed cases + 200-key randomized property test against `std::set` — `make test` |
| `Makefile`  | `make`, `make run`, `make test`, `make clean` |

## Build & Run

```bash
make           # builds ./btree
make run       # interactive menu (insert / delete / search / inorder / levels)
make test      # builds ./test_runner and runs the suite
make clean
```

`make test` should print:

```
All tests passed
```

## Public API

```cpp
class BalancedTree {
public:
    explicit BalancedTree(int t);   // minimum degree (t >= 2)

    void insert(int key);
    void remove(int key);
    bool search(int key);

    void print_inorder();
    void print_levels();
    void collect_inorder(std::vector<int>& out);
};
```

## Operations at a glance

- **Search.** Linear scan within a node, recurse into the correct child.
  `O(log_t n)` node visits, `O(t log_t n)` comparisons.
- **Insert.** Walks root → leaf. Any full node (`2t-1` keys) encountered is
  pre-emptively split, its median promoted into the parent. Guarantees the
  parent always has room when the leaf split lands.
- **Delete.** If the key sits in a leaf, remove it. If it's in an internal
  node, swap with predecessor or successor (whichever child still has `>= t`
  keys) and recurse. On the way down, any child with `< t` keys is repaired
  by borrowing from a sibling, or merging with one if no sibling can spare a
  key. This keeps the path-down invariant that the current node always has
  enough keys to lose one safely.

## Invariants enforced

1. Every node (except the root) has between `t-1` and `2t-1` keys.
2. All leaves are at the same depth.
3. Keys within a node are sorted ascending.
4. For an internal node with keys `k_0 < k_1 < … < k_{n-1}`, child `i` holds
   keys in `(k_{i-1}, k_i)`.

The 200-key randomized test cross-checks `collect_inorder()` against
`std::set` after a random mix of insert and delete operations — any
violation of (2)/(3)/(4) shows up as an inorder mismatch.

## Why a B-Tree in a DBMS course?

B-Trees minimize disk I/O: each node maps cleanly onto a single page (a few
KB), so a tree with branching factor in the hundreds reaches billions of
keys in 3–4 hops. That makes them the default secondary-index structure in
every mainstream OLTP engine. This lab implements the in-memory variant so
the algorithmic core (split/borrow/merge) is isolated from the page
manager.
