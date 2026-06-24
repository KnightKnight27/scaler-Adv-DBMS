# Lab 6: B-Tree Implementation

**Student:** Dhruv Davda
**Roll No:** 24BCS10203
**Language:** C++17

A B-tree over integer keys. The program is driven by a small interactive menu
that supports insertion, deletion, search, a sorted (in-order) dump, and a
level-by-level dump of the tree.

## Files

| File | Purpose |
|---|---|
| `main.cpp` | `BTree` class (node layout + all operations) and the menu driver |
| `Makefile` | Build, run, and clean targets |
| `README.md` | Design notes and usage |

## Build and run

```bash
cd Lab6/24bcs10203-DhruvDavda
make
make run
```

Or compile by hand:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o btree
./btree
```

The very first value the program reads is the **minimum degree `t`**, which must
be at least `2`.

## Menu

```text
1) Insert
2) Delete
3) Search
4) Print sorted (in-order)
5) Print by level
6) Quit
```

`Print sorted` lists the keys in ascending order. `Print by level` prints every
level of the tree as bracketed nodes, which makes splits, merges, and borrows
easy to watch.

## What a B-tree guarantees

For a minimum degree `t`:

- Keys inside a node are kept sorted.
- A non-root node holds between `t - 1` and `2t - 1` keys.
- A node with `k` keys has exactly `k + 1` children.
- Every leaf is at the same depth.
- The keys in a child subtree lie strictly between the two separator keys that
  surround that child in the parent.

Because each node can pack many keys, the height stays close to `log_t n`. That
is exactly why B-trees back database indexes: one node maps neatly onto one disk
page, so a lookup touches only a handful of pages.

This implementation stores **distinct** keys; inserting a key that already
exists is reported and ignored.

## How the operations work

### Insert (top-down, pre-emptive split)

While walking down toward a leaf, any child that is already full (`2t - 1` keys)
is split *before* we descend into it, pushing its median key up to the parent.
Because full nodes are cleared out on the way down, the target leaf always has
room for the new key. When the root itself is full it is split first and a new
root is created above it — the only situation in which the tree gets taller.

For `t = 3` a full node has five keys:

```text
before: [10 20 30 40 50]
after:  [10 20]   (30 rises)   [40 50]
```

### Delete (CLRS cases)

- **Key in a leaf** — remove it directly.
- **Key in an internal node** — replace it with its in-order predecessor or
  successor when the neighbouring child has at least `t` keys, then delete that
  surrogate further down.
- **Both neighbouring children minimal** — merge them with the separator key and
  continue the deletion inside the merged node.
- **Before descending** into a child that has only `t - 1` keys, top it up to at
  least `t` keys by borrowing from a sibling, or by merging with a sibling.

If the root loses its last key after a merge, its single remaining child becomes
the new root — the mirror image of the root split done during insertion.

## Sample session

Input (degree `3`, then a few operations):

```text
3
1
10
1
20
1
30
1
40
1
50
1
60
5
4
2
30
5
6
```

Behaviour after the inserts — `Print by level` then `Print sorted`, then delete
`30` and dump the levels again:

```text
Level 0: [30]
Level 1: [10 20] [40 50 60]
10 20 30 40 50 60
Level 0: [40]
Level 1: [10 20] [50 60]
```

## Complexity

Let `n` be the number of keys and `t` the minimum degree.

| Operation | Time |
|---|---|
| Search | `O(t log_t n)` |
| Insert | `O(t log_t n)` |
| Delete | `O(t log_t n)` |
| In-order traversal | `O(n)` |

The height factor `log_t n` is tiny for index-sized `t`, since each node holds
many keys.
