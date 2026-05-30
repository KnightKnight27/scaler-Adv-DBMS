# B-Tree (CLRS Chapter 18)

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048
**Date:** 2026-05-30

---

## 1. Objective

Implement a B-tree of minimum degree t in C++. The tree must support:
- Insert with preemptive splitting (CLRS 18.3)
- Search returning the node and index
- Remove handling all three CLRS cases

---

## 2. Build and Run

```bash
g++ -std=c++17 -O2 -o btree btree.cpp
./btree
```

The program inserts 18 keys into a t=3 B-tree, prints the in-order and level-order views, searches for several keys, removes 5 keys, and shows the result.

---

## 3. Why B-Trees in Databases

A B-tree with minimum degree t stores up to 2t-1 keys per node. With t=512 and 8 KB pages each node holds up to 1023 keys. A table with 1 billion rows needs a tree of depth log_1024(1e9) ~ 3. So any lookup by primary key touches at most 3-4 pages -- the same whether the table has 1 million or 1 billion rows.

This is why:
- **SQLite** stores every table as a B-tree where each page is one node (4 KB default).
- **InnoDB** (MySQL) uses a B+ tree as its clustered index.
- **PostgreSQL** uses a B-tree variant for all btree indexes.
- **LevelDB / RocksDB** use LSM-trees (log-structured merge trees), which are B-tree cousins optimised for write-heavy workloads.

The key difference from a binary search tree: wide nodes mean shallow trees, and each node is sized to match one disk page. A disk read fetches a whole page at once, so reading 1023 keys costs the same as reading 1.

---

## 4. B-Tree Invariants (minimum degree t)

| Property | Rule |
|----------|------|
| Keys per non-root node | t-1 <= n <= 2t-1 |
| Keys in root | 1 <= n <= 2t-1 |
| Children per internal node | t <= n+1 <= 2t |
| Leaf depth | All leaves at same depth |
| Key ordering | keys[i-1] < all keys in child[i] < keys[i] |

When t=3: nodes hold 2 to 5 keys. This is a good "small" value for demonstrations since splits are visible quickly.

---

## 5. Insert -- Preemptive Split

### 5.1 Algorithm

New keys are always inserted at a leaf. To avoid walking back up the tree:
- **Split any full node (2t-1 keys) we pass through on the way down**, before we need to use it.
- This guarantees that when we arrive at a node's parent to promote a median, the parent is not full.

```
insert(key):
    if root is full (2t-1 keys):
        create new empty root s
        make old root a child of s
        split_child(s, 0)   -- split the old root
        insert_nonfull(s, key)
    else:
        insert_nonfull(root, key)

insert_nonfull(x, key):
    if x is a leaf:
        shift keys right, insert key in sorted position
    else:
        find child[i] that should contain key
        if child[i] is full:
            split_child(x, i)
            if key > x.keys[i]: i++
        insert_nonfull(child[i], key)
```

### 5.2 Split diagram

When child y (full: 2t-1 keys) of node x is split at index i:

```
Before split:
  x:  [ ... | ki | ... ]
               |
       y: [k1 k2 k3 k4 k5]   (2t-1 = 5 keys when t=3)
           c1 c2 c3 c4 c5 c6

After split_child(x, i):
  x:  [ ... | ki | k3 | ki+1 | ... ]   <- k3 (median) promoted
               |         |
   y: [k1 k2]         z: [k4 k5]
      c1 c2 c3            c4 c5 c6
```

The median key `k3` moves up into x. Left half stays in y, right half goes to new node z.

---

## 6. Remove -- Three Cases

### 6.1 Case 1: Key is in a leaf

Simple. Delete the key in place. The leaf may now have t-2 keys, which is fine if we guaranteed t-1 before descending (see Case 3).

### 6.2 Case 2: Key is in an internal node

Let the key be `k` at position i in node x.

| Subcase | Condition | Action |
|---------|-----------|--------|
| 2a | child[i] has >= t keys | Replace k with predecessor (max of child[i] subtree), delete predecessor recursively |
| 2b | child[i+1] has >= t keys | Replace k with successor (min of child[i+1] subtree), delete successor recursively |
| 2c | Both children have t-1 keys | Merge child[i], k, child[i+1] into one node. Delete k from merged node. |

### 6.3 Case 2c merge diagram

```
Before merge (t=3, so t-1 = 2 keys per child):
  x: [ ... | k | ... ]
              |   \
         [a b]   [c d]   <- both have t-1 = 2 keys

After merge:
  x: [ ... ]             <- k removed from x
         |
    [a b k c d]          <- single merged node, delete k from here
```

### 6.4 Case 3: Key is not in current node x, recurse into child[i]

Before recursing, ensure child[i] has at least t keys (so it can lose one without violating invariants):

| Subcase | Condition | Action |
|---------|-----------|--------|
| 3a-L | Left sibling has >= t keys | Borrow: rotate key down from x[i-1] into child[i], rotate child[i-1]'s rightmost key up to x |
| 3a-R | Right sibling has >= t keys | Borrow: rotate key down from x[i] into child[i], rotate child[i+1]'s leftmost key up to x |
| 3b | Both siblings have t-1 keys | Merge child[i] with one sibling (pull separator key from x into the merged node) |

### 6.5 Borrow from left sibling diagram

```
Before borrow:
  x: [ ... | sep | ... ]
              |     \
     sibling:[...|last]   child:[first|...]

After borrow_from_prev:
  x: [ ... | last | ... ]
                |     \
  sibling:[...]     child:[sep | first | ...]
```

The separator key `sep` descends into the front of child, and sibling's rightmost key ascends to replace it in x.

---

## 7. Sample Output

```
============================================================
  B-Tree (minimum degree t = 3)
============================================================

  t = 3: each node holds 2..5 keys, 3..6 children.

[PHASE 1] Insert
------------------------------------------------------------
  insert(10)
  insert(20)
  insert(5)
  ...
  insert(22)

  In-order: 1 3 5 6 7 10 12 15 17 20 22 25 28 30 35 40 45 50

  Tree structure (level-order):
  [17]
      [5, 10]
          [1, 3]
          [6, 7]
          [12, 15]
      [28, 40]
          [20, 22, 25]
          [30, 35]
          [45, 50]

[PHASE 2] Search
------------------------------------------------------------
  search(17) -> found (node key[0] = 17)
  search(25) -> found (node key[2] = 25)
  search(99) -> not found

[PHASE 3] Remove
------------------------------------------------------------
  remove(6)  -> removed
  remove(17) -> removed
  remove(30) -> removed
  remove(1)  -> removed
  remove(50) -> removed

  In-order: 3 5 7 10 12 15 20 22 25 28 35 40 45

  Tree structure (level-order):
  [20]
      [7, 12]
          [3, 5]
          [10]
          [15]
      [35, 40]
          [22, 25, 28]
          [35]
          [45]

[PHASE 4] Edge cases
------------------------------------------------------------
  remove(99) (absent) -> not found
  re-insert(17)
  In-order: 3 5 7 10 12 15 17 20 22 25 28 35 40 45

============================================================
  Done.
============================================================
```

---

## 8. Complexity

| Operation | Time | Notes |
|-----------|------|-------|
| Search | O(t * log_t n) | Linear scan in each node; O(log n) with binary search |
| Insert | O(t * log_t n) | At most one split per level, O(log_t n) levels |
| Remove | O(t * log_t n) | At most one merge/borrow per level |
| Space | O(n) | n keys total, distributed across nodes |

With t proportional to page size (e.g., t=512 for 8 KB pages), `log_t(n)` is tiny even for billion-row tables. This is why disk-based databases use B-trees and not binary search trees.

---

## 9. B-Tree vs B+ Tree

Real database engines typically use a B+ tree variant:
- All data is in the leaves (internal nodes hold only routing keys).
- Leaf nodes are linked in a doubly-linked list for efficient range scans.
- This allows `BETWEEN`, `ORDER BY`, and `LIMIT` queries to scan leaves sequentially after a single root-to-leaf descent.

Our implementation is a pure B-tree (data in all nodes, no leaf linking), which is simpler to implement and sufficient to illustrate the core algorithms.

---

## 10. Files in this Submission

| File | Description |
|------|-------------|
| `btree.cpp` | C++ implementation of the B-tree |
| `Makefile` | Build instructions |
| `Assignment.md` | This document |
| `README.md` | Quick-start guide |

---

## 11. References

- Cormen, T.H. et al. *Introduction to Algorithms*, 3rd ed., Ch. 18 (B-Trees)
- Comer, D. "The Ubiquitous B-Tree." ACM Computing Surveys, 11(2), 1979.
- SQLite file format: https://www.sqlite.org/fileformat2.html
- PostgreSQL B-tree index internals: https://www.postgresql.org/docs/current/btree.html
