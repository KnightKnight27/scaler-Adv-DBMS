# Lab 4 — Red-Black Tree & B-Tree

## Files
- `rbt.cpp`    — Red-Black Tree (insert, delete, inorder)
- `btree.cpp`  — Full B-Tree with insert/split, delete/merge/borrow, search
- `build.cpp`  — build helper (prints compile commands)

## Build & Run
```bash
g++ -std=c++17 -o rbt   rbt.cpp    && ./rbt
g++ -std=c++17 -o btree btree.cpp && ./btree
```

## Red-Black Tree properties
1. Every node is Red or Black.
2. Root is Black.
3. No two consecutive Red nodes.
4. Every path from a node to NULL descendants has the same black-height.

Guarantees O(log n) height.

## B-Tree properties (order t)
- Every internal node: t–1 to 2t–1 keys, t to 2t children.
- Every leaf: t–1 to 2t–1 keys.
- Root: as few as 1 key.

PostgreSQL B-Tree index pages are 8 KB by default — matching `PRAGMA page_size`
from Lab 2 — so one disk read fetches an entire B-Tree node.

## When to use which

| Property        | Red-Black Tree              | B-Tree (order t)                  |
|-----------------|-----------------------------|-----------------------------------|
| Storage         | In-memory                   | Disk-optimized (large node = page)|
| Node size       | 1 key per node              | Up to 2t–1 keys per node          |
| Height          | O(log n)                    | O(log_t n) — shorter for big t    |
| DB use          | In-memory indexes           | On-disk indexes (PG, MySQL, InnoDB)|
| Cache friend.   | Poor (pointer chasing)      | Excellent (sequential in one node)|
| Balance ops     | Rotation + recolor          | Split / borrow / merge            |

## Key takeaways
- RB-Tree: color + rotation = in-memory sorted map (std::map).
- B-Tree: packs many keys per node = minimizes disk I/O.
- B-Tree delete: borrow from sibling, merge with sibling, or replace with
  predecessor/successor.
- `T` (minimum degree) controls fanout: higher T → shorter tree → fewer disk seeks.
