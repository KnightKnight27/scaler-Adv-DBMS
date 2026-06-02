# Lab 6: B-Tree Index — Notes

**Name:** Siddhanth Kapoor
**Roll Number:** 10154

`main.cpp` implements a B-tree of minimum degree `t` (template parameter) storing
integer keys with string values, used as an index. Build with the included CMake or:

```bash
c++ -std=c++17 -O2 main.cpp -o btree && ./btree
```

The demo uses `t = 3`, so every node holds between `t-1 = 2` and `2t-1 = 5` keys.

## B-tree properties maintained

1. Keys within a node are stored in sorted order.
2. A node with `n` keys has `n+1` children (non-leaf).
3. All leaves are at the same depth.
4. Every node except the root has between `t-1` and `2t-1` keys.
5. The tree stays balanced because growth happens by **splitting**, not by adding depth at one spot.
6. Search/insert are O(log n) in node accesses.

## Task mapping

- **Initialization:** `root` starts as an empty leaf; `T` fixes node capacity.
- **Insertion:** `insert` splits a full root first (the only way height grows), then `insertNonFull` walks down, splitting any full child before descending — so we never recurse into a full node.
- **Node splitting:** `splitChild` moves the right half of a full child into a new node and **promotes the median key** into the parent. This is what keeps all leaves level.
- **Search:** `search` does a linear scan within a node, then follows the correct child pointer; it prints every node it visits.
- **Structure analysis:** `print` shows the tree level by level.
- **Indexing behaviour:** keys stay globally ordered and each level narrows the search range — exactly how a database index reduces disk reads.

## Observed run

Inserting 15 records produces a 2-level tree:

```
L0: 10 20 40
  L1: 1 3 5 6 7
  L1: 12 17
  L1: 25 30
  L1: 45 50 55
```

The three medians `10, 20, 40` were promoted into the root as the leaves overflowed,
every leaf sits at level 1 (balanced), and `search(45)` reaches its value in just two
node accesses.
