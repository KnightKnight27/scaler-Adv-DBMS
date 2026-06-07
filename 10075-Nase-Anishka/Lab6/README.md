# LAB 6 — B-TREE INDEX

> Roll Number: **10075** &nbsp;&nbsp;|&nbsp;&nbsp; Name: **Nase Anishka**
>
> A templated **B-tree index** in C++17 — `key -> value`, where the value
> plays the part of a row / record pointer the way a real database index
> maps a search key to a tuple's location. It supports **insert** (with
> in-place update on a repeated key), **search**, ordered **traversal**,
> two structure printers, and a `validate()` that actually checks every
> B-tree invariant and throws if one is broken. Four demo scenarios show
> the splits happening live, narrate the search path, and prove the tree
> stays balanced even on the input that destroys a plain BST.

---

# WHY THIS STRUCTURE IS *THE* DATABASE INDEX

Almost every disk-based index you'll meet is a B-tree or its sibling the
B⁺-tree:

* **SQLite** stores every table and every index as a B-tree of pages —
  the same on-disk format I dissected by hand in Lab 4.
* **PostgreSQL**'s default index (`CREATE INDEX`) is a B-tree (Lehman &
  Yao's concurrent variant).
* **MySQL/InnoDB** clusters the whole table in a B⁺-tree keyed by the
  primary key.
* File systems (NTFS, HFS+, btrfs, XFS) index directories and extents
  with B-trees.

The reason is **disk economics**. A binary search tree does one key
comparison per node and therefore one potential disk seek per level — on
a million keys that's ~20 seeks. A B-tree packs *hundreds* of keys into
one node (one disk page), so its fan-out is huge and its height is tiny:
the same million keys fit in a tree only **2–3 levels deep**. Fewer
levels means fewer page reads, and page reads are the entire cost model
of a database. A B-tree is "the shape you get when you redesign a search
tree so that each node is exactly one disk page."

---

# THE DEGREE, AND THE CAPACITY IT IMPLIES

The tree is parameterised by a **minimum degree** `t` (`t >= 2`). Every
node then obeys:

| quantity                 | minimum   | maximum   |
|--------------------------|-----------|-----------|
| keys per node            | `t - 1`   | `2t - 1`  |
| children per node        | `t`       | `2t`      |

with the root excused from the lower bound (it may hold as few as one
key). In the first demo `t = 3`, so every node carries **2–5 keys and up
to 6 children**; the `printStructure()` output shows leaves with exactly
5 keys, which is the full-node boundary right before a split.

A node is split the moment it would overflow past `2t - 1`, and the
**median** key is pushed up into the parent — that single rule is what
keeps every leaf at the same depth.

---

# THE B-TREE PROPERTIES (and how this code keeps them)

1. **Keys inside a node are sorted** — insertion shifts larger keys right
   and drops the newcomer into the gap; `validate()` re-checks strict
   order.
2. **An internal node with `k` keys has exactly `k + 1` children**, and
   the `i`-th key separates child `i` from child `i + 1`. `validate()`
   carries a `(low, high)` window down the recursion and asserts every
   key falls inside it.
3. **All leaves are at the same depth.** This is the balance guarantee.
   `validate()` returns each subtree's leaf-depth and throws the moment
   two siblings disagree.
4. **Key counts stay in `[t-1, 2t-1]`** (root exempted on the low side).
5. **The tree stays balanced after every insert**, because growth happens
   by splitting the *root* (the only way the height ever increases), not
   by adding a deeper leaf on one side.

---

# FILES IN THIS FOLDER

* `main.cpp` — the `BTreeIndex<Key, Value>` template plus a driver that
  runs four scenarios end to end.
* `CMakeLists.txt` — C++17 with `-Wall -Wextra -Wpedantic`.
* `.gitignore` — build artefacts.
* `run_output.txt` — captured stdout from a real run, so the tree shapes
  and split traces are visible without rebuilding.
* `README.md` — this file.

---

# BUILD AND RUN

```bash
cd 10075-Nase-Anishka/Lab6
cmake -B build -S .
cmake --build build
./build/btree_index
```

Or without CMake:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -o btree_index main.cpp
./btree_index
```

---

# WALKTHROUGH OF THE FOUR SCENARIOS

## 1. Build a `t = 3` index and watch the splits

Inserting `50 30 70 20 40 60 80 10 25 35 45 55` fires exactly two splits:

```text
insert 60
    split -> [20 | 30]  ^40^  [50 | 70]
...
insert 55
    split -> [45 | 50]  ^60^  [70 | 80]
```

* The first split happens because the root leaf had filled to
  `[20 | 30 | 40 | 50 | 70]` (5 keys = `2t-1`). Inserting `60` overflows
  it, so the median **40** is promoted into a brand-new root and the leaf
  splits in two — this is the only event that ever grows the height.
* The second split is of an *internal* leaf the same way, promoting **60**
  alongside 40.

Final shape — root with two keys, three leaves all on the same level:

```text
[40 | 60]
    [10 | 20 | 25 | 30 | 35]  (leaf)
    [45 | 50 | 55]  (leaf)
    [70 | 80]  (leaf)
```

```text
in-order keys: 10 20 25 30 35 40 45 50 55 60 70 80
size=12  height=1  nodes=4
validate(): OK — all B-tree invariants hold
```

Twelve keys live in a tree only **one level deep** — that is the high
fan-out paying off.

## 2. Search: the path walked, and node accesses counted

`findTraced()` prints every node it touches:

```text
search 45: [40 | 60] -> child 1  [45 | 50 | 55]  -> HIT (2 node accesses)
search 50: [40 | 60] -> child 1  [45 | 50 | 55]  -> HIT (2 node accesses)
search 99: [40 | 60] -> child 2  [70 | 80]  -> MISS (2 node accesses)
```

Every search — hit *or* miss — costs at most `height + 1` node accesses
(here 2), because each node read narrows the search to exactly one child.
That "reduce the search space at each level" behaviour is the whole point
of an index, and it's why a miss is just as cheap as a hit.

## 3. Sorted input — the BST's worst case, the B-tree's non-event

Ascending `1..20` is the input that turns a plain BST into a height-19
linked list. A `t = 2` B-tree (a 2-3-4 tree) shrugs it off:

```text
  L0: [8]
  L1: [4] [12]
  L2: [2] [6] [10] [14 | 16 | 18]
  L3: [1] [3] [5] [7] [9] [11] [13] [15] [17] [19 | 20]
size=20  height=3  nodes=17
validate(): OK — every leaf sits at the same depth
```

Height **3** instead of 19, and every one of the ten leaves sits on the
same bottom row — visible proof of the balance invariant.

## 4. Re-inserting a key updates the payload, not the shape

```text
before: 40 -> row#40, size=12
after : 40 -> row#40-UPDATED, size=12  (size unchanged — it was an update)
```

`insert()` first does a lookup; if the key already exists it overwrites
the value and returns, so the tree behaves like a map (unique keys),
which is what a primary-key index wants.

---

# IMPLEMENTATION NOTES

## Proactive, top-down splitting

I split a full child **before** descending into it (the CLRS approach),
rather than inserting first and fixing overflow on the way back up. The
payoff is that insertion is a **single root-to-leaf pass** with no
parent back-pointers and no second upward walk — by the time I reach the
leaf, every node above it already has room for a median to be pushed up
if needed. The root is the special case: when *it* is full I allocate a
new root over it and split, which is the only place the tree gets taller.

## Parallel `keys[]` / `vals[]` arrays

Each node keeps the sort keys in one vector and the payloads in a second,
index-aligned vector, instead of one vector of `{key, value}` structs.
That mirrors how a real database page keeps the comparison key separate
from the row data, and it keeps the hot path — comparing keys during a
search — touching only the small `keys[]` array.

## `validate()` checks all five invariants

Most B-tree code online only *builds* the tree. Like in my RB-tree lab, I
wanted a function that would actually *catch* a broken split, so
`validate()` walks the tree carrying a `(low, high)` key window and
asserts: lengths of `keys[]`/`vals[]` agree, key counts are within
`[t-1, 2t-1]`, keys are strictly sorted and inside their separator
window, child count equals `keys + 1`, and — the important one — **all
leaves return the same depth**. A bad `splitChild` would trip the
depth-mismatch or the separator-window check immediately.

## Generic over `Key`, comparisons via `operator<` only

Search and ordering use only `a < b` (equality is `!(a<b) && !(b<a)`), so
the same template indexes `int -> string` in scenario 1 and `int -> int`
in scenario 3, and would take `std::string` keys unchanged.

---

# OBSERVATIONS

* **Page size / node capacity.** With `t = 3` a node holds 2–5 keys; the
  full leaf `[10 | 20 | 25 | 30 | 35]` is sitting exactly at the `2t-1`
  ceiling, one insert away from splitting.
* **Page count / node count.** 12 keys → 4 nodes; 20 keys → 17 nodes. The
  number of nodes grows with the data but the *height* barely moves.
* **Tree depth.** height 1 for 12 keys, height 3 for 20 keys — and a
  million keys at `t = 3` would still be only ~9 levels.
* **Balance.** Every leaf prints on the same bottom level in both the
  indented and the BFS view.
* **Search cost.** Constant within a level: `height + 1` node accesses for
  any key, present or absent.

---

# WHAT I LEARNED

* A B-tree is really **"a search tree whose node is sized to a disk
  page."** Once that clicked, every design choice (high fan-out, splitting
  on overflow, keys-in-sorted-runs) follows from "minimise page reads."
* **Splitting upward is what guarantees balance.** A BST grows a longer
  branch on one side; a B-tree can only get taller by splitting the root,
  so all leaves rise together and stay level. That's why sorted input —
  fatal to a BST — does nothing special to a B-tree.
* The **median promotion** is the heart of it: pushing the middle key up
  is what keeps both halves legal (`t-1` keys each) after a split.
* Writing `validate()` alongside the tree was, again, the most valuable
  habit — the depth-equality check is exactly the invariant a subtle
  split bug would violate silently, and having it run on every scenario
  let me trust the output instead of eyeballing tree drawings.
* I implemented **insert + search + update**, which is what the lab asks
  for and what a write-once/read-many index needs. **Delete** is the
  natural next step and is genuinely harder — it needs borrowing from a
  sibling or merging two nodes to keep every node above the `t-1` floor,
  the mirror image of the split logic. CLRS 18.3 covers it; I'd add that
  next.
