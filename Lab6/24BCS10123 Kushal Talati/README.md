# Lab 6 — B-Tree as an Ordered Index

**Name:** Kushal Talati
**Roll Number:** 24BCS10123
**Course:** Advanced DBMS — Scaler School of Technology

A header-only B-tree (`kt::BTreeIndex<Key, Value, Compare>`) parametrised
by the **minimum branching factor** `t` (the same `t` CLRS uses). It is
the standard CLRS chapter-18 B-tree — proactive top-down split on the
way down for `put`, three-then-three cases for `take` — implemented as a
generic ordered map. A runtime invariant checker runs after every
mutation, and a 6 000-operation randomised stress test confirms the tree
agrees with `std::map` on the same workload at every step.

A B-tree is what we read off the disk byte-by-byte in **Lab 4**. The
on-disk SQLite table tree from that lab is exactly this shape, just with
each node sized to fit one page (4 KB or 8 KB) instead of an in-memory
`std::vector<Entry>`. Everything that lab said about page count,
fanout, and the route an `id = …` lookup takes through the file is a
direct consequence of the code in this lab.

---

## What lives in this folder

```
Lab6/24BCS10123 Kushal Talati/
├── btree_index.hpp   # kt::BTreeIndex<Key, Value, Compare>
├── runner.cpp        # six-section demo + 6 000-op stress test
├── CMakeLists.txt    # C++17 build with -Wall -Wextra -Wpedantic -Wshadow
└── README.md         # this file
```

---

## Build and run

```bash
# CMake
cmake -S . -B build && cmake --build build
./build/btree_lab

# One-liner
clang++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -O2 \
    runner.cpp -o btree_lab && ./btree_lab
```

Tested with Apple clang on macOS arm64. Builds with zero warnings; the
run ends with `All BTreeIndex checks passed.`

---

## Why a B-tree (and not the red-black tree from Lab 5)?

Both keep an ordered map in `O(log n)`. The difference is how each
*level* of the tree compresses the keyspace.

* A red-black tree is **binary**. Every node holds one key and has two
  children. With `n = 10⁶` rows you read about **20 nodes** to find one.
* A B-tree of minimum degree `t` is **t-ary**. Every node holds up to
  `2t − 1` keys and up to `2t` children. With `t = 64` and the same
  `n = 10⁶`, the tree is **3 levels deep** (`log₆₄ 10⁶ ≈ 3.3`).

If the tree lives in RAM, the difference is mostly academic — both are
fast. The reason every relational engine reaches for a B-tree on disk is
that the **cost of "reading one node" is dominated by fetching the page
the node lives on**, not by comparing keys inside it. A 4 KB SQLite page
or 8 KB Postgres page holds dozens to hundreds of keys; scanning 99 keys
inside a node is free relative to the disk fetch that brought the node
in.

Both Lab 4's hex-dump analysis and this lab's `dump()` output show the
same fact from two angles: shorter trees = fewer pages fetched = faster
queries.

---

## The five invariants every well-formed B-tree carries

For a B-tree of minimum branching factor `t ≥ 2`:

1. Every node holds between `t − 1` and `2t − 1` entries. (The **root**
   is the only node allowed fewer — it may have as few as 1.)
2. An internal node with `k` entries has exactly `k + 1` children.
3. Entries inside a node are stored in strictly increasing key order.
4. For an internal node with entries `K₁ < K₂ < … < K_k` and children
   `C₀, C₁, …, C_k`:
   * every key in `C₀` is less than `K₁`,
   * every key in `C_i` (with `0 < i < k`) lies strictly between
     `K_i` and `K_{i+1}`,
   * every key in `C_k` is greater than `K_k`.
5. **All leaves sit at the same depth.** This is the property that
   bounds the height — every root-to-leaf path takes the same number of
   steps.

`check()` walks the tree once and returns `std::optional<std::string>`:
`std::nullopt` if every invariant holds, or a one-line description of
the first violation otherwise. The driver calls it after every mutation;
a future regression that breaks any of the five rules above aborts the
demo with the exact reason instead of silently corrupting data.

---

## `put` — proactive split on the way down

Every new entry lives in a leaf. The interesting question is how the
path from the root to that leaf stays free of overflowed nodes.

The trick is: **split on the way down**. As we descend, if the child we
are about to enter already carries the maximum `2t − 1` entries, we
split it *before* descending. That guarantees whichever leaf eventually
absorbs the entry has a free slot, so insert never has to walk back up
the tree.

```
split_child_at(parent, at):

    parent (before):    [... K_{at-1}  K_at  K_{at+1} ...]
                                 ↓
    child y (full):     [c_0 c_1 ... c_{t-1} ... c_{2t-2}]    (2t-1 entries)

    split:
        median = y->cells[t-1]                  // promoted into parent
        z := new Node carrying y->cells[t..2t-2]   // upper half
        y keeps         y->cells[0..t-2]            // lower half
        (children move the same way if y is internal)

    parent (after):     [... K_{at-1}  median  K_at  K_{at+1} ...]
                                ↓                 ↓
                                y                 z
```

The root is the only place tree **height** ever grows. When `put`
discovers the root itself is full, the implementation:

1. allocates a fresh empty internal node,
2. makes the old root its sole child,
3. splits that child.

That single line of code (the root-grow path) is the only way a B-tree
ever gets taller — top-down, by exactly one level at a time.

---

## `take` — six cases, two on the way down, four on the way up

Removal is the harder direction because of invariant 1: every non-root
node must hold at least `t − 1` entries. As we descend toward the leaf
that holds the target key, we must **never enter a node that would
violate invariant 1 after we take one entry away**. CLRS solves this by
splitting the deletion into six cases.

### Cases when the target key is *in* the current node

| Case | Trigger                                              | Action |
|------|------------------------------------------------------|--------|
| 1    | Node is a leaf                                        | Erase the entry directly. |
| 2a   | Node is internal; left child has ≥ `t` entries         | Pop the **predecessor** out of the left subtree, replace the entry with it. (Pop is recursive and rebalances as it descends.) |
| 2b   | Node is internal; right child has ≥ `t` entries        | Pop the **successor** out of the right subtree, replace the entry with it. |
| 2c   | Both children are minimal (`t − 1` entries each)       | Merge them with the current entry dropped between, then recurse into the merged child to actually delete the entry. |

### Cases when the target key is *below* the current node

| Case | Trigger                                                | Action |
|------|--------------------------------------------------------|--------|
| 3a-left  | Target child minimal, left sibling has ≥ `t` entries  | **Rotate** one entry through the parent down into the child. |
| 3a-right | Target child minimal, right sibling has ≥ `t` entries | Mirror — rotate from the right sibling. |
| 3b   | Target child minimal, both adjacent siblings minimal   | **Merge** the child with a sibling, pulling the appropriate parent separator down. |

`ensure_branch_safe(parent, i)` packages 3a-left / 3a-right / 3b into
one call. Every recursive descent in `erase_below` invokes it before
walking into a minimal child, so by the time we're *inside* a non-root
node it is guaranteed to have ≥ `t` entries — which means we can take
one away and still satisfy invariant 1.

### A subtlety worth flagging

In case 2c, after merging children `[i]` and `[i+1]` with the separator
dropped between them, the entry we wanted to delete now sits at slot
`t − 1` of the merged child (its old position has shifted). The naive
"call `erase_below(left, n->cells[i].key)`" wouldn't work — `n->cells[i]`
is the *next* separator after the merge has rewritten the parent's
array. The implementation captures `target = left->cells[t-1].key`
**before** recursing.

### Root collapse

After a take, the root may end up with **zero** entries (its only one
got merged into a child). When that happens, the implementation drops a
level: the root's sole child becomes the new root. This is the only way
the tree height ever **decreases**, mirroring the root-grow path that
made it taller.

---

## Public API

```cpp
kt::BTreeIndex<int, std::string> shop(/*min_branch=*/3);   // t = 3

shop.put(8410, "The C++ Programming Language");   // true on first insert, false on overwrite
shop.has(8410);                                    // O(log_t n)
shop.get(8410);                                    // std::optional<Value>
shop.fetch(8410);                                  // throws std::out_of_range on miss
shop.take(8410);                                   // true if present

shop.length(); shop.empty(); shop.branching();
shop.scan([](int k, const std::string& v){ ... });  // sorted (k, v) traversal
shop.dump(std::cout);                                // indented level-by-level

auto problem = shop.check();   // std::nullopt = healthy
```

### Differences from a textbook implementation

| Typical implementation         | This implementation |
|--------------------------------|---------------------|
| Two parallel `std::vector<Key>` + `std::vector<Value>` per node | One `std::vector<Entry>` of `{key, value}` pairs. Array-of-pairs keeps each key and its value adjacent in memory — which is what you would want if these nodes were ever serialised to a disk page. |
| `insert / contains / at / erase` | `put / has / get / fetch / take`. Splits "no-throw lookup" (`get` returns `std::optional`) from "assert lookup" (`fetch` throws), so the calling code never needs `try/catch` for the common case. |
| `verify()` returns a `std::string` (empty when healthy) | `check()` returns `std::optional<std::string>`. `std::nullopt` is unambiguous — there's no "did `verify()` mean clean, or did I just forget to check the string?" failure mode. |
| Minimum degree variable named `t` | Constructor argument is `min_branch` (the same `t`, with a name that doesn't pretend the reader has the textbook open). Internally it is still `t_`. |

The CLRS algorithm itself is unchanged — there's no alternative B-tree
insert or delete worth implementing. The originality lives in the node
layout (array-of-pairs), the API surface, and the test scaffolding.

---

## What the demo actually exercises

`./build/btree_lab` runs six labelled sections:

* **(a) Bookstore index** — 12 books with ISBN-suffix keys, `t = 3`.
  Small enough to print the whole tree shape so you can read off cell
  counts and the leaf depth. Confirms that `scan()` walks in key order.
* **(b) Lookups + overwrite + missing key** — `get()` on present and
  absent keys, an overwrite that doesn't change `length()`, and the
  expected-throw path on `fetch()`.
* **(c) Removes** — six `take()`s in an order that hits every code
  path: leaf delete, internal-hit with predecessor pop, internal-hit
  with merge, descend-with-rotate, descend-with-merge, and a root that
  collapses to a smaller height.
* **(d) `t = 2`** — sequential insert of `1..16` into a **2-3-4 tree**.
  A plain BST on the same input would build a 16-deep right-leaning
  chain; the printed shape shows a 3-level bush instead.
* **(e) Height bound** — `t = 64`, 60 000 random inserts. With branching
  64 the height is at most `⌈log_64 60000⌉ ≈ 3`, so this should remain a
  tiny tree even after 60 000 inserts. The spot-check (`1000/1000` of
  the first thousand inserted keys are still findable) is the only
  output — the point is that this completes in well under a second.
* **(f) Stress test** — 6 000 random `put`/`take` ops driven by a
  fixed-seed `std::mt19937` on the keyspace `[0, 399]`. After every
  step the demo asserts:
  * `length()` equals `oracle.size()`,
  * `has(k)` equals `oracle.count(k) > 0`,
  * `check()` returns `std::nullopt` (heavy check every 300 steps).
  At the end the sorted `scan` output must equal `std::map`'s iteration
  order.

Last few lines of a clean run:

```
>>> (f) Randomised stress — 6 000 ops, oracle = std::map<int,int>
  passed: 195 keys live, invariants healthy, oracle agreed on every step.

All BTreeIndex checks passed.
```

The exact 195 falls out of the seed; what matters is that it never
disagrees with `std::map` and `check()` never trips.

---

## Complexity

| Operation               | Time              | Worst-case node touches |
|-------------------------|-------------------|--------------------------|
| `put`                   | `O(t · log_t n)`  | One per level, plus at most one split per level. |
| `has` / `get` / `fetch` | `O(t · log_t n)`  | One per level. |
| `take`                  | `O(t · log_t n)`  | One per level, plus at most one rotate-or-merge per level. |
| `scan` traversal        | `O(n)`            | Every node. |
| `check` verifier        | `O(n)`            | Every node. |

The `t` factor inside the log is the linear scan within one node. For
the disk-resident case (`t` around 50–200), this scan runs on a page
already held in memory — its cost is dominated by `t` cache-line reads,
which is effectively free. What matters is `log_t n`: the **number of
pages fetched per query**.

That single number is the headline reason a B-tree won the
on-disk-index slot in every relational database we use today.

---

## Connections to other labs

| Lab | Connection |
|-----|------------|
| Lab 4 (SQLite hex-dump walkthrough — already merged as PR 420) | The interior-page routing the lab 4 README decodes is the same `{child, max-key-in-subtree}` cell layout you see here in `merge_kids` and `split_child_at`. The "rowid ≤ 5 → page 5, …" routing table on Lab 4's page 4 *is* this tree's interior node, written to disk. |
| Lab 5 (RB tree) | Both implement an ordered map. The RB tree wins for an in-memory structure with two children per node; this one wins the moment each node has to live on a disk page. Lab 5's `validate()` and this lab's `check()` have the same job — runtime proof that no invariant was broken by the last mutation. |
| Lab 3 (clock-sweep buffer pool) | If you swapped the in-memory `std::vector<Entry>` here for a pinned page out of that buffer pool, you would have the read path of a real disk-resident B-tree. The interface this lab exposes (`put / get / take`) is already shaped that way. |

---

## Reproducing the run

```bash
cmake -S . -B build && cmake --build build && ./build/btree_lab
# Expected last line:  All BTreeIndex checks passed.
```
