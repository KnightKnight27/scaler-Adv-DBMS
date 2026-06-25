# Lab 4 — Red-Black Tree & Full B-Tree

**Rohan Ranjan — 24BCS10428**

## Objective
Implement a Red-Black Tree (the self-balancing BST behind in-memory ordered maps) and a
full B-Tree (the on-disk index structure used by PostgreSQL, MySQL, and SQLite),
supporting insert, search, and delete with rebalancing.

## Files
| File        | Purpose                                                            |
|-------------|--------------------------------------------------------------------|
| `rbt.cpp`   | Red-Black Tree: insert + delete with recolor/rotation fixups       |
| `btree.cpp` | B-Tree (minimum degree `T`): insert (split), search, delete        |

## Build & run
```bash
g++ -std=c++17 -o rbt   rbt.cpp   && ./rbt
g++ -std=c++17 -o btree btree.cpp && ./btree
```

## Part 1 — Red-Black Tree
Invariants that guarantee O(log n) height:
1. Every node is Red or Black.
2. The root is Black.
3. No two consecutive Red nodes (a Red node's parent is Black).
4. Every path from a node to its NULL descendants has the same number of Black nodes.

Insertion restores the invariants via the three classic cases (recolor → rotate parent →
rotate grandparent) and their mirrors; deletion uses `fix_delete` with the sibling cases.

## Part 2 — Full B-Tree (minimum degree T)
Every node holds `T-1 … 2T-1` keys; internal nodes have `T … 2T` children. The root may
hold as few as 1 key.
- **insert** — split any full child (`2T-1` keys) on the way down, promoting its median.
- **search** — descend, choosing the child between bracketing keys.
- **delete** — three cases: replace with predecessor/successor (when a child has ≥ T
  keys), or borrow from a sibling, or merge two children around the separating key.

> Note: the lab-text `split_child` had a double-assignment bug (it re-read
> `y->keys.begin()+T` *after* `y` had already been resized to `T-1`, which is out of
> range). This implementation promotes the median once, then assigns the right half to the
> new node before resizing `y`.

## Red-Black Tree vs B-Tree — when to use which
| Property           | Red-Black Tree            | B-Tree (order T)                          |
|--------------------|---------------------------|-------------------------------------------|
| Storage            | In-memory                 | On-disk (a large node = one page)         |
| Node size          | 1 key per node            | up to `2T-1` keys per node                |
| Height             | O(log n)                  | O(log_T n) — much shorter for large T     |
| Use in databases   | In-memory indexes, std::map | On-disk indexes (PostgreSQL, InnoDB)    |
| Cache friendliness | Poor (pointer chasing)    | Excellent (sequential keys in one node)   |
| Rebalance          | Rotation only             | Child split / sibling borrow / merge      |

PostgreSQL's B-Tree index pages are 8 KB by default (matching `page_size` from Lab 2), so
one disk read fetches an entire B-Tree node.

## Key takeaways
- Red-Black Trees keep balance with color + rotation; ideal for in-memory sorted maps.
- B-Trees minimize disk I/O by packing many keys into one node = one page read.
- B-Tree delete has three cases: borrow, merge, or replace with predecessor/successor.
- A larger `T` → shorter tree → fewer disk seeks.
