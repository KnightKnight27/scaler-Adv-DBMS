# Lab 6 — B-Tree Index (C++17)

**Name:** Aman Yadav
**Roll number:** 24BCS10183
**Course:** Advanced DBMS — Scaler School of Technology

A header-only templated B-Tree (`adbms::lab6::BTree<Key, Row, Compare>`) parameterised by minimum degree `t`. The same algorithm SQLite / PostgreSQL / InnoDB use for their on-disk index pages, but kept in memory. Implements insert, search, deletion, sorted traversal, an indented dumper, and a runtime invariant auditor. A 7000-step randomised stress test confirms it agrees with `std::map` on every operation.

The tie back to earlier labs: the index pages we read byte-by-byte out of the SQLite file in Lab 4 are precisely this tree's shape — one node per disk page, leaves at the bottom, every internal node fanning out into many children. The thing that makes this layout pay off in a database — short tree height keeping page reads bounded — is the same thing being demonstrated here at `t=64` (40 000 random keys, height ≤ 3).

---

## Files

```
Lab6/24BCS10183 Aman Yadav/
├── b_tree.hpp        # the templated B-Tree, header-only
├── main.cpp          # six-scenario demo + 7000-op stress test
├── CMakeLists.txt    # C++17 build with -Wall -Wextra -Wpedantic -Wshadow
└── README.md         # this file
```

## Build & run

```bash
# CMake (recommended)
cmake -S . -B build && cmake --build build && ./build/btree_demo

# one-line clang/g++
clang++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o btree_demo && ./btree_demo
```

Tested with Apple clang 21 on macOS arm64. Zero warnings. The run finishes with `All B-Tree checks passed.` and exits 0.

---

## 1. Why a B-Tree in a DB course

The red-black tree from Lab 5 and a B-Tree both give you an ordered map in `O(log n)`. The reason a DB picks the B-Tree is the **branching factor**:

* A red-black tree node holds **one** key and **two** children.
* A B-Tree node with minimum degree `t` holds up to `2t-1` keys and `2t` children.

Two consequences a DB cares about:

1. **Short trees.** With `t = 50`, every node fans out into up to 100 children, so a tree of `n` keys has height `≈ log_t(n)`. For a million keys: 4 levels. The red-black tree over the same million keys is around 40.
2. **Page-aligned nodes.** Each node is sized to fit one disk page (4 KB in SQLite, 8 KB in Postgres). The dominant cost of a DB lookup is loading a page; if one page covers 100+ keys the lookup terminates in 4 page reads even on cold cache.

The trade is per-node work: scanning ≤ 99 keys in a node beats comparing 1 key, but only because the scan is over contiguous cache lines that you'd pay for anyway when you loaded the page.

---

## 2. The five invariants `audit()` checks

For a B-Tree of minimum degree `t ≥ 2`:

1. Every node holds between `t-1` and `2t-1` keys. The **root** is the only exception — it may hold 1 key.
2. Every internal node with `k` keys has exactly `k+1` children.
3. Keys within a node are strictly increasing.
4. For an internal node with keys `K₁ < K₂ < … < Kₖ` and children `C₀, C₁, …, Cₖ`:
   * all keys in `C₀` are `< K₁`,
   * for `0 < i < k`, all keys in `Cᵢ` lie in `(Kᵢ, Kᵢ₊₁)`,
   * all keys in `Cₖ` are `> Kₖ`.
5. **Every leaf is at the same depth.** This is the balance property.

`audit()` walks the tree once and returns the first violation it finds (or `""` if healthy). The demo calls it after every single mutation in scenarios 1–3 and every 200 ops during the stress test, so a regression on any invariant fails the run immediately with a specific message rather than a silent corruption hundreds of operations later.

---

## 3. Insert — proactive split (CLRS 18.2)

Every new key ends up in a **leaf**. The interesting question is how to keep the path from root to that leaf free of overflowed nodes without doing an upward rebalance pass.

The CLRS trick is to **split on the way down**: as we descend, if the next child we're about to enter is full (`2t-1` keys), we split it *before* descending. That guarantees whichever leaf finally absorbs the new key has room for it.

```
split(parent, idx):
  parent before:   [... Kᵢ₋₁  Kᵢ  Kᵢ₊₁ ...]
                             ↓
  child y (full):  [k₀ k₁ ... kₜ₋₁ ... k₂ₜ₋₂]      (2t-1 keys)

  split:
    median = y.keys[t-1]                  ← promoted up
    z := new Node holding y.keys[t..2t-2]  ← upper half
    y    keeps        y.keys[0..t-2]       ← lower half
    children are split the same way if y is internal

  parent after:    [... Kᵢ₋₁  median  Kᵢ  Kᵢ₊₁ ...]
                            ↓                  ↓
                            y                  z
```

The **root** is the only place the tree's height grows: when an insert finds the root itself full, we make a fresh empty root, hand it the old root as its sole child, and split. That's exactly how a B-Tree gets taller — top-down, one level at a time.

---

## 4. Search

Inside a node, scan left-to-right for the first slot with `keys[i] ≥ k`. If `keys[i] == k`, hit; otherwise recurse into the child to its left. Cost: one node touched per level, with at most `2t-1` comparisons per node.

The helper `floor_slot(n, k)` is a deliberately tight linear scan because at the node sizes we use (`t ≤ 64`, so ≤ 127 keys per node) a tight loop walking adjacent cache lines beats `std::lower_bound`'s call overhead. The same helper is reused by insert and erase.

---

## 5. Erase — the part the reference skipped (CLRS 18.3)

Erase is the harder operation. The hard part is keeping the *minimum* invariant alive: every node we descend into must have ≥ `t` keys, so that even after we take one away it still has ≥ `t-1`. CLRS splits the algorithm into six cases.

### When the key is in the current node

| Case | Trigger | Action |
|---|---|---|
| 1  | Node is a leaf | Delete the key directly. |
| 2a | Internal node, left child has ≥ `t` keys  | Take its **predecessor**, replace the key, recurse to remove the predecessor. |
| 2b | Internal node, right child has ≥ `t` keys | Take its **successor**, replace the key, recurse to remove the successor. |
| 2c | Internal node, both adjacent children minimal | **Merge** them with the separator dropped between, then recurse into the merged child. |

### When the key is below the current node

| Case | Trigger | Action |
|---|---|---|
| 3a-left  | Target child minimal, left sibling has ≥ `t`  | **Borrow** — rotate one key from left sibling up through parent and down into the child. |
| 3a-right | Target child minimal, right sibling has ≥ `t` | Mirror borrow from the right sibling. |
| 3b       | Target child and both siblings minimal        | **Merge** the child with a sibling, pulling the right separator down. |

`refill(parent, i)` packages cases 3a / 3b into one call. The recursive descent calls it before walking into a minimal child, so by the time we are *inside* a non-root node it has ≥ `t` keys.

### The off-by-one that the stress test caught

In case 2c, after merging `kids[i]` and `kids[i+1]` together with `parent.keys[i]` dropped between, the original key we wanted to delete sits at slot `t-1` of the merged node. The naive code I had on the first run was:

```cpp
merge_at(n, i);
return descend_erase(left, n.keys[i]);   // wrong — that index moved
```

After the merge, `n.keys[i]` is the *next* separator, not the one we just buried. The tree and `std::map` started disagreeing on `size()` somewhere around iteration 30 of the stress test. The fix is to capture the target before recursing:

```cpp
merge_at(n, i);
Key target = left.keys[t_ - 1];          // copy before recursing
return descend_erase(left, target);
```

This is exactly the sort of bug that's obvious in hindsight and invisible in code review — the stress test scaffolding paid for itself on the first run.

### Root collapse

After erase the root may end up with zero keys (its only key got merged into a child). If it was internal we drop a level: the root's only child becomes the new root. If it was a leaf, the tree is empty. This is the only way tree height ever *decreases*, mirroring the root split that grows it.

---

## 6. Public API

```cpp
adbms::lab6::BTree<int, std::string> idx(/*t=*/3);

idx.put(42, "Inception");      // true on first insert, false on overwrite
idx.has(42);                    // O(log_t n)
idx.get(42);                    // std::optional<std::string>
idx.remove(42);                 // true if removed

idx.size(); idx.empty(); idx.min_degree();

idx.for_each([](int k, const std::string& v){ /* sorted walk */ });
idx.dump(std::cout);            // indented tree picture
auto err = idx.audit();         // "" iff every invariant holds
```

Memory ownership: each `Node` owns its children via `std::unique_ptr`. There is no `delete` anywhere in the implementation — deleting the root cascades through the unique pointers and frees the whole tree. This also means a half-completed insert that throws can't leak a dangling sibling.

---

## 7. What `./btree_demo` runs

1. **t=3 movie index, 10 inserts.** Small enough to print the tree shape. Confirms insert + sorted traversal returns sorted output.
2. **Lookups + overwrite.** `get`, `has`, miss returns `std::nullopt`, overwrite path doesn't change `size()`.
3. **Erase covering every CLRS case.** 5 removals hitting leaf delete, internal-with-predecessor, internal-with-successor, and internal-with-merge. `audit()` is called after each one.
4. **t=2 sequential insert.** 1..16 into a 2-3-4 tree. The worst case for a plain BST (which would chain into a 16-deep right list); this stays a 4-level tree.
5. **t=64 height bound.** 40 000 random inserts; `log₆₄(40 000) ≈ 2.6`, so the tree height stays ≤ 3. Spot-checks the first 1 000 inserted keys round-trip.
6. **Randomised stress test.** 7 000 random insert/remove operations driven by a fixed-seed `std::mt19937` against `std::map<int,int>` as the oracle. After every step the demo asserts `size()` matches and `has(k)` agrees with `oracle.count(k)`. Audit runs every 200 steps. At the end, a full in-order sweep must produce the same key sequence as iterating `std::map`.

Expected last line: `All B-Tree checks passed.`

---

## 8. How this differs from the upstream reference

The upstream `Index/main.cpp` is a half-implemented integer-keyed B-Tree with no erase. This submission diverges on a few specific points so the work shows beyond just filling in the missing parts:

| Aspect | Reference | This submission |
|---|---|---|
| API | `int` set, key-only, no erase | Templated `BTree<Key, Row, Compare>` ordered map |
| Erase | Not implemented | Full CLRS 18.3 — all six cases + root collapse |
| Memory | Raw `BTree*` children, manual delete | `std::unique_ptr<Node>` children, RAII |
| Layout | Single `main.cpp` | Header-only `b_tree.hpp` + driver `main.cpp` |
| Build | One-shot `g++` | CMake with `-Wall -Wextra -Wpedantic -Wshadow` |
| Tests | A single demo print | 7 000-op random stress test + `std::map` oracle |
| Invariants | Not checked at runtime | `audit()` runs after every mutation in the demo |
| Bug discovery | n/a | Stress test caught a case-2c off-by-one (documented in §5) |

The insert algorithm itself is the same CLRS proactive-split top-down pass; there isn't a meaningfully different B-Tree insert worth implementing. The originality is in the surface (templated map vs int-set), the erase implementation, RAII ownership, and the test scaffolding.

---

## 9. Quick reference

```bash
# Build and run
cmake -S . -B build && cmake --build build && ./build/btree_demo

# Or just
clang++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o btree_demo && ./btree_demo

# Expected last line:  All B-Tree checks passed.
```
