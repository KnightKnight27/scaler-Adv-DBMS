# Lab 6 — B-Tree Index

**Name:** Akshat Kushwaha
**Roll No:** 24BCS10060

## What this lab is about

A B-Tree is the data structure databases actually use for indexes on disk
(PostgreSQL, MySQL/InnoDB and SQLite all use a B-Tree variant). The big idea: a
single node holds *many* keys instead of one, so the tree is very short and a
lookup only has to read a few nodes. Since each node is sized to fit one disk
page, "few nodes" means "few disk reads". This lab is my B-Tree with insert,
search and delete.

## Files

| File | What it does |
|---|---|
| `btree.cpp` | `BTree` class (insert / search / remove / level + sorted print) + demo |

## Build & run

```bash
g++ -std=c++17 -Wall -Wextra btree.cpp -o btree
./btree
```

Output (with minimum degree `t = 2`, a 2-3-4 tree):

```
inserting: 10 20 5 6 12 30 7 17 3 1 25 40 50 22

tree by level (shows fanout and height):
L0: [10]
L1: [6] [20 30]
L2: [1 3 5] [7] [12 17] [22 25] [40 50]

in-order (sorted): 1 3 5 6 7 10 12 17 20 22 25 30 40 50

search 17 = found
search 99 = not found

removing 6 and 20...
L0: [17]
L1: [5 10] [30]
L2: [1 3] [7] [12] [22 25] [40 50]
in-order after removals: 1 3 5 7 10 12 17 22 25 30 40 50
```

You can see 14 keys fit in just **3 levels**. A binary tree would be much taller.

## The rules (minimum degree `t`)

- A node has at most `2t-1` keys (and `2t` children).
- Every node except the root has at least `t-1` keys.
- A node with `k` keys has exactly `k+1` children (unless it's a leaf).
- All leaves are at the same depth — that's the balance property.

With `t = 2` that means 1–3 keys per node, i.e. a 2-3-4 tree.

## Insert — split on the way down

The trick is to keep the path I'm walking free of full nodes:

1. If the **root** is full (`2t-1` keys), make a new empty root above it and
   split the old root in two. This is the *only* way the tree grows taller.
2. Walking down to a leaf, whenever the child I'm about to enter is full, I
   **split** it first: its middle key moves up into the parent and the rest
   becomes a new sibling. That guarantees the leaf I finally land in has room.

```
split a full child:
   parent: [ ...  Kmid  ... ]      <- middle key promoted up
              /          \
         left half    right half   <- the full child becomes two
```

Because splits push the middle key *up*, the tree grows from the root, which is
why all leaves always stay at the same level.

## Search

Inside a node, scan the keys left to right until I find the key or pass it; if I
pass it, drop into the child between the two keys. One node per level, so the
whole search is `O(log_t n)` — and since `t` is large in a real DB, the tree is
only a few levels deep.

## Delete — borrow or merge

Deletion is the fiddly part. The rule I keep is: before descending into a child,
make sure that child has at least `t` keys, so it can afford to lose one.
`ensure_min` handles that by either:
- **borrowing** a key from a neighbouring sibling that has spare keys (rotating
  it through the parent), or
- **merging** the child with a sibling and pulling the separator key down when
  no sibling has spares.

If the key to delete is in an internal node, I replace it with its predecessor or
successor (the largest key on the left / smallest on the right) and then delete
*that* from the leaf — the same trick as a normal BST delete. If the root ends up
empty after a merge, I drop a level. The demo deletes 6 and 20 and the in-order
output is still perfectly sorted, which shows the structure stayed correct.

## Why a B-Tree instead of the red-black tree from Lab 5

| | Red-Black tree | B-Tree |
|---|---|---|
| Keys per node | 1 | up to `2t-1` (many) |
| Where it lives | in memory | designed for disk (1 node = 1 page) |
| Height for n keys | ~`log₂ n` | ~`log_t n` (much shorter) |
| Disk reads per lookup | many (pointer chasing) | few (one page per level) |

A taller tree means more page reads, and a disk read is thousands of times slower
than a memory access. By packing ~100s of keys into one page-sized node, a B-Tree
keeps even a million-row index just 3–4 levels deep.

## Key takeaways

- A B-Tree node holds many keys so the tree is short — the whole point is to
  minimise disk reads.
- Insert splits full nodes on the way down; the tree only grows at the root,
  which keeps all leaves at the same depth.
- Delete keeps every node at the minimum size by borrowing from or merging with
  siblings.
- Higher minimum degree `t` → bigger nodes → shorter tree → fewer disk seeks.
