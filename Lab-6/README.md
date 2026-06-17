# Lab 6 — B-Tree (C++17)

**Name:** Gauri Shukla
**Roll Number:** 24BCS10115
**Course:** Advanced DBMS — Scaler School of Technology

An in-memory **B-Tree of minimum degree `t`** holding a set of distinct integer
keys. It supports `insert`, `remove`, `contains`, an in-order traversal, a
level-order pretty-printer, and a runtime `validate()` that re-checks every
B-Tree invariant. Insert uses CLRS proactive splitting (chapter 18.2); remove
implements the full CLRS deletion (chapter 18.3) with predecessor/successor
replacement, sibling borrowing, and merging.

This is the on-disk sibling of the **Red-Black Tree from Lab 5**. Same `O(log n)`
balanced-ordered-map guarantee, same `.h`/`.cc`/`main.cc`/`Makefile` layout —
but where the RB-tree is binary (one key per node), the B-Tree packs up to
`2t − 1` keys into every node so that one node maps onto one disk page. The
table B-trees I dumped byte-by-byte in **Lab 4** (SQLite hex dump) are exactly
this structure, just persisted to disk.

---

## Files

```
Lab-6/
├── BTree.h     # class declaration + node layout + invariants
├── BTree.cc    # insert (split), remove (CLRS 18.3), search, print, validate
├── main.cc     # 5-scenario demo + 8 000-op stress test vs std::set
└── Makefile    # c++17 build, -Wall -Wextra -Wpedantic -Wshadow
```

## Build & run

```bash
make run
# or, manually:
c++ -std=c++17 -O2 -Wall -Wextra -Wpedantic main.cc BTree.cc -o btree_demo && ./btree_demo
```

Tested with Apple clang 21 on macOS arm64 — zero warnings, exits `0`, prints
`All B-Tree checks passed.`

---

## 1. Why a B-Tree and not the Lab-5 Red-Black Tree?

Both stay balanced in `O(log n)`. The difference is the **fan-out**:

| | Red-Black Tree (Lab 5) | B-Tree (this lab) |
|---|---|---|
| Keys per node | 1 | up to `2t − 1` |
| Children per node | ≤ 2 | up to `2t` |
| Height over `n` keys | `~2·log₂ n` | `~log_t n` |
| Where it lives | RAM (`std::map`) | disk (one node = one page) |

A database lookup is dominated by **page reads**, not comparisons. If one page
holds 100+ keys (`t ≈ 50`), a million-key index is only ~4 levels deep, so a
cold lookup costs ~4 page fetches. The Lab-5 RB-tree over the same million keys
is ~40 pointers deep — 40 potential cache/disk misses. That gap is exactly why
SQLite, PostgreSQL and InnoDB index on B-Trees (or the B⁺ variant).

## 2. Invariants

For minimum degree `t ≥ 2`:

1. every node has `t − 1 … 2t − 1` keys (the **root** may have as few as 1),
2. an internal node with `k` keys has exactly `k + 1` children,
3. keys within a node are strictly increasing,
4. a separator key sits between the ranges of its two adjacent children,
5. **all leaves are at the same depth** (the balance property).

`BTree::validate()` walks the whole tree and returns the first violated rule (or
`""`). `main.cc` calls it after *every* insert and remove, so any code path that
ever breaks an invariant aborts the run immediately.

## 3. Insert — split on the way down

Every key lands in a leaf. To avoid an upward rebalancing pass, we split any
**full** (`2t − 1`-key) child *before* descending into it, so the node that
finally absorbs the key always has a spare slot. The median of a split is
promoted into the parent. The only place the tree grows taller is the root: when
the root itself is full, a fresh empty root adopts it as a child and splits it —
height increases by exactly one level (`insert` in `BTree.cc`).

## 4. Remove — the CLRS cases

Deletion's hard part is never dropping a node below `t − 1` keys. The descent
keeps every node it enters at `≥ t` keys via `ensure_fat()`:

- **Key in a leaf** → erase it directly.
- **Key in an internal node**, a child has `≥ t` keys → replace it with the
  **predecessor** (rightmost key of the left child) or **successor** (leftmost
  key of the right child), then delete that key from the child.
- **Key in an internal node**, both surrounding children minimal → **merge**
  them around the separator and recurse into the merged node.
- **Descending into a minimal child** → first **borrow** a key from a sibling
  with spare keys (`rotate_from_left` / `rotate_from_right`), or, if both
  siblings are minimal, **merge** with one of them.

If the root loses its last key (its only key was merged into a child), the tree
**collapses one level** — the root's sole child becomes the new root. This is
the mirror of the root-split that grows the tree, and the only way height
decreases.

## 5. Public API

```cpp
BTree t(/*min_degree=*/3);
t.insert(42);          // duplicates are ignored (set semantics)
t.contains(42);        // O(log_t n) descent
t.remove(42);          // true if a key was removed
t.size(); t.empty(); t.degree(); t.height();
t.inorder();           // std::vector<int> of keys, sorted
t.print(std::cout);    // level-order picture, L0 / L1 / ...
t.validate();          // "" when healthy, else the first broken invariant
```

The level-order printer reuses the BFS idea from my Lab-5 RB-tree printer — one
line per depth, each node shown as `{k1 k2 ...}`:

```
L0: {30 60 75}
L1: {5 10 15 20 25} {35 40 45 50 55} {65 70} {80 85 90 95 100}
```

## 6. What `./btree_demo` runs

1. **t=3, 20 inserts** — prints the tree shape, in-order walk, and shows that a
   duplicate insert is ignored.
2. **Search** — hits and misses.
3. **Remove** — eight deletions chosen to hit leaf-delete, predecessor,
   successor, merge, sibling-borrow, and a root-collapse.
4. **t=2 (a 2-3-4 tree)** — inserts `1..20` in sorted order. A plain BST would
   become a 20-deep right chain; the B-Tree stays 4 levels tall.
5. **Stress test** — 8 000 random insert/remove ops against `std::set` as the
   oracle (fixed seed `20250617`). After every step it checks `size()` and the
   remove return value, periodically runs `validate()`, and at the end asserts
   the in-order traversal equals the sorted `std::set`.

## 7. Complexity

| Operation | Time | Notes |
|---|---|---|
| `insert` | `O(t · log_t n)` | ≤ one split per level on the way down |
| `contains` | `O(t · log_t n)` | one node touched per level |
| `remove` | `O(t · log_t n)` | ≤ one borrow/merge per level |
| `inorder` / `validate` | `O(n)` | visits every node |

The `t` factor is the in-node linear scan; on a real disk-resident tree that
scan runs over a single cached page, so the figure that actually matters is
`log_t n` — the number of **pages fetched**.

## 8. Design notes (how this differs from a key-only reference)

- **Modular `.h`/`.cc` split** instead of header-only, matching my Lab-5 layout,
  built with a plain `Makefile` rather than CMake.
- **Set of ints**, consistent with the int Red-Black Tree from Lab 5, rather
  than a templated key→value map.
- **`validate()` with full range-bound checking** (each subtree is verified
  against the `(lo, hi)` window implied by its parent separators), not just a
  per-node key-order check.
- **Stress oracle is `std::set`** with an in-order equality check at the end.

The insert/delete algorithms themselves are the standard CLRS ones — there is no
meaningfully different B-Tree algorithm — so the originality here is the surface,
the invariant checker, the printer, and the test harness.
