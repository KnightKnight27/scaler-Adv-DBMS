# Lab 6: B-Tree Implementation

**Student:** Saswata Das  
**Roll No:** 24BCS10248  
**Language:** C++17

This lab implements a B-tree for integer keys. The program supports insertion,
deletion, search, sorted traversal, and level-order display through an
interactive menu.

## Files

| File | Purpose |
|---|---|
| `main.cpp` | B-tree node structure, tree operations, and menu driver |
| `Makefile` | Build, run, and clean commands |
| `README.md` | Design notes and usage instructions |

## Build And Run

```bash
cd Lab6/24bcs10248-SaswataDas
make
make run
```

Manual build:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o btree
./btree
```

The first input is the minimum degree `t`. It must be at least `2`.

## Menu

```text
1. Insert
2. Delete
3. Search
4. Print inorder
5. Print levels
6. Exit
```

`Print inorder` shows keys in sorted order. `Print levels` prints each B-tree
level, which makes splits, merges, and borrowing easier to inspect.

## B-Tree Rules

For minimum degree `t`:

- Every node stores keys in sorted order.
- A non-root node stores at least `t - 1` keys and at most `2t - 1` keys.
- An internal node with `k` keys has exactly `k + 1` children.
- All leaves appear at the same depth.
- Keys in each child subtree stay between the separator keys around that child.

These rules keep the tree shallow. That is why B-trees are useful for database
indexes: one node can map closely to one disk page, so a lookup needs only a
small number of page reads.

## Insert

Insertion is top-down. Before the algorithm descends into a full child, it
splits that child and promotes the median key to the parent. Because full nodes
are handled on the way down, the final leaf always has space for the new key.

For `t = 3`, a full node has five keys:

```text
before: [10 20 30 40 50]
after:  [10 20]  promoted 30  [40 50]
```

If the root is full, a new root is created first. This is the only case where
the tree height grows.

## Delete

Deletion follows the standard B-tree cases:

- If the key is in a leaf, remove it directly.
- If the key is in an internal node, replace it with either its predecessor or
  successor when that child has enough keys.
- If both adjacent children are minimal, merge them with the separator key and
  continue deleting inside the merged node.
- Before descending into a child, ensure that child has at least `t` keys by
  borrowing from a sibling or merging with a sibling.

If the root becomes empty after a merge, its only child becomes the new root.
This is the matching shrink operation for root splits during insertion.

## Sample Session

Example input:

```text
3
1 10
1 20
1 30
1 40
1 50
1 60
5
4
3 40
2 30
5
6
```

Expected behavior:

```text
Level 0: [30]
Level 1: [10 20] [40 50 60]
10 20 30 40 50 60
Found
Level 0: [40]
Level 1: [10 20] [50 60]
```

## Complexity

Let `n` be the number of keys and `t` be the minimum degree.

| Operation | Time |
|---|---|
| Search | `O(t log_t n)` |
| Insert | `O(t log_t n)` |
| Delete | `O(t log_t n)` |
| Inorder traversal | `O(n)` |

The `log_t n` height term is small for database indexes because each node can
hold many keys.
