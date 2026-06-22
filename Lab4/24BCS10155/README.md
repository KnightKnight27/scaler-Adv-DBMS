# Lab Session 4: Red-Black Tree & Full B-Tree in C++

**Name:** Snehangshu Roy
**Roll No:** 24BCS10155

## Objective
Implement a Red-Black Tree (the self-balancing BST behind in-memory indexes and
`std::map`) and a full B-Tree (the on-disk index structure used by PostgreSQL,
MySQL/InnoDB and SQLite) with insert, search, and delete (split / borrow / merge).

## Files
- `rbt.cpp` — Red-Black Tree (insert, delete with fixups, inorder dump with colors).
- `btree.cpp` — B-Tree of minimum degree `T` (insert, search, delete).
- `makefile` — build / run both.

## Build & Run
```bash
make            # builds rbt and btree
make run        # runs both
# or individually:
g++ -std=c++17 -o rbt rbt.cpp && ./rbt
g++ -std=c++17 -o btree btree.cpp && ./btree
```

## Part 1 — Red-Black Tree properties
1. Every node is Red or Black.
2. The root is Black.
3. No two consecutive Red nodes.
4. Every path from a node to its NULL descendants has the same number of Black nodes.

These invariants guarantee O(log n) height. Balance is restored on insert/delete
via recoloring and rotations.

## Part 2 — B-Tree (minimum degree t)
Every internal node holds `t-1 .. 2t-1` keys and `t .. 2t` children; the root may
hold as few as 1 key.
- **Insert:** split a full child (2t-1 keys) on the way down, promoting its median.
- **Delete:** ensure each node descended into has ≥ t keys by borrowing from a
  sibling or merging; keys in internal nodes are replaced by predecessor/successor.

> Note: the lab text's `split_child` had a redundant "redo" block; this submission
> uses a single clean split (promote `keys[T-1]`, move the right half to a new node).

## Red-Black Tree vs B-Tree — when to use which

| Property           | Red-Black Tree              | B-Tree (order t)                            |
|--------------------|-----------------------------|---------------------------------------------|
| Storage            | In-memory                   | Designed for disk (large node = 1 page)     |
| Node size          | 1 key per node              | up to `2t-1` keys per node                  |
| Height             | O(log n)                    | O(log_t n) — much shorter for large t       |
| Use in databases   | In-memory indexes, std::map | On-disk indexes (PostgreSQL, MySQL, InnoDB) |
| Cache friendliness | Poor (pointer chasing)      | Excellent (sequential keys in one node)     |
| Rebalance          | Rotation only               | Child split / sibling borrow / merge        |

PostgreSQL's B-Tree index pages are 8 KB by default (matching `page_size` from
Lab 2), so one disk read fetches an entire B-Tree node.

## Key Takeaways
- Red-Black Trees maintain balance via color + rotation; ideal for in-memory sorted maps.
- B-Trees minimize disk I/O by packing many keys into one node = one page read.
- B-Tree delete has three cases: borrow from sibling, merge with sibling, or
  replace with predecessor/successor.
- The minimum degree `T` controls fanout and height — higher `T` → shorter tree → fewer seeks.
