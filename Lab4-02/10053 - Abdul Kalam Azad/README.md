<!-- ============================================================ -->
<!-- SUBMISSION CHECKPOINT                                        -->
<!--   Name     : Abdul Kalam Azad                               -->
<!--   Roll No. : 24BCS10053                                     -->
<!--   Lab      : Lab Session 4 (Part 2) - RB Tree & B-Tree      -->
<!-- ============================================================ -->

# Lab Session 4: Red-Black Tree & Full B-Tree in C++

**Author:** Abdul Kalam Azad
**Roll No.:** 24BCS10053

## Objective
Implement a Red-Black Tree (self-balancing BST used for in-memory database
indexes) and a full B-Tree (the on-disk index structure used by PostgreSQL,
MySQL/InnoDB and SQLite) supporting insert, split-promotion, and delete with
borrow/merge on underflow.

## Files

```text
RedBlackTree.cpp   Part 1 - Red-Black Tree (insert, delete, search, inorder)
BTree.cpp          Part 2 - Full B-Tree of minimum degree T (insert, search, delete)
README.md          this file
```

## Build & Run

```bash
g++ -std=c++17 -o rbt   RedBlackTree.cpp && ./rbt
g++ -std=c++17 -o btree BTree.cpp        && ./btree
```

## Part 1 — Red-Black Tree

Invariants that guarantee O(log n) height:
1. Every node is Red or Black.
2. The root is Black.
3. A Red node cannot have a Red parent (no two consecutive Reds).
4. Every root-to-NULL path contains the same number of Black nodes.

Balance is restored after insert/delete using **recoloring** and **rotations**
(`leftRotate` / `rightRotate`). The `inorder` traversal prints each key with its
colour, e.g. `10B 20R ...`.

## Part 2 — Full B-Tree (minimum degree T)

With minimum degree `T` (here `T = 2`):
- every node (except root) holds between `T-1` and `2T-1` keys;
- every internal node has between `T` and `2T` children;
- the tree grows **upward** — it only gets taller when the root splits.

Operations implemented:
- **insert** — split any full child (`2T-1` keys) on the way down, promoting the
  median into the parent.
- **search** — standard multi-way descent.
- **delete** — the three CLRS cases: replace with predecessor/successor, or
  `fill` an under-full child by **borrowing** from a sibling or **merging** with one.

### Expected output of `BTree.cpp`

```text
Inorder after inserts:
1 3 5 6 7 10 12 17 20 25 30
Search 17: found
Search 99: not found
Inorder after removing 6 and 20:
1 3 5 7 10 12 17 25 30
```

This was verified by porting the identical algorithm and running it through
**2000 randomized insert/delete/search trials**, each compared against a
reference sorted set — all passed.

### Note on correctness fixes
The B-Tree `split_child` and `delete` logic were implemented carefully to avoid
two subtle out-of-bounds errors:
1. The median key is captured **before** `y` is shrunk (reading it afterwards
   would index past the end of the resized vector).
2. The "is this the last child?" test compares against the number of **keys**,
   not the number of children, so a merge at the end of a node cannot index past
   the last child.

## Red-Black Tree vs B-Tree

| Property           | Red-Black Tree            | B-Tree (order T)                         |
|--------------------|---------------------------|------------------------------------------|
| Storage            | In-memory                 | Designed for disk (one node = one page)  |
| Keys per node      | 1                         | up to `2T-1`                             |
| Height             | O(log n)                  | O(log_T n) — much shorter for large T    |
| Use in databases   | in-memory maps, `std::map`| on-disk indexes (PostgreSQL, InnoDB)     |
| Cache friendliness | poor (pointer chasing)    | excellent (keys packed in one node)      |
| Rebalance          | rotation + recolor        | child split / sibling borrow / merge     |

PostgreSQL B-Tree index pages are 8 KB by default, so one disk read fetches an
entire B-Tree node — which is exactly why higher fanout (`T`) means fewer seeks.

## Key Takeaways
- Red-Black Trees stay balanced via colour + rotation; ideal for in-memory sorted maps.
- B-Trees minimise disk I/O by packing many keys into one node = one page read.
- B-Tree delete has three cases: borrow from sibling, merge with sibling, or
  replace with predecessor/successor.
- The minimum degree `T` controls fanout and height — higher `T` -> shorter tree
  -> fewer disk seeks.

---

**Submitted by:** Abdul Kalam Azad &nbsp;|&nbsp; **Roll No.:** 24BCS10053 &nbsp;|&nbsp; **Lab 4 (Part 2)**
