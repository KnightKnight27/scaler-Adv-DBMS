# Lab 4 — Red-Black Tree + B-Tree (Insert, Split, Borrow, Merge, Delete)

## Overview

This lab implements two fundamental tree data structures used in database indexing:

1. **Red-Black Tree** — A self-balancing BST used in in-memory indexing (e.g., `std::map`, Java `TreeMap`)
2. **B-Tree** — A balanced multi-way tree optimized for disk-based storage (used in PostgreSQL, MySQL, SQLite)

## Red-Black Tree

### Properties
1. Every node is RED or BLACK
2. Root is always BLACK
3. Every NIL leaf is BLACK
4. No two consecutive RED nodes (red-red violation forbidden)
5. All root-to-leaf paths have the same black-height

### Operations
- **Insert**: BST insert + fixup (recolor / rotate)
- **Delete**: BST delete + fixup (4 cases per side)
- **Search**: Standard BST search O(log n)

### Fixup Cases (Insert)
```
Case 1: Uncle is RED → recolor parent, uncle, grandparent
Case 2: Uncle is BLACK, z is right child → left-rotate parent
Case 3: Uncle is BLACK, z is left child → right-rotate grandparent + recolor
```

## B-Tree

### Properties (minimum degree t)
- Each node has at most **2t - 1** keys and **2t** children
- Each non-root node has at least **t - 1** keys
- All leaves are at the same level
- Keys within a node are sorted

### Operations

| Operation | Description |
|-----------|-------------|
| **Insert** | Find leaf position; if node is full (2t-1 keys), **split** before inserting |
| **Split** | Promote median key to parent, divide node into two |
| **Delete** | 3 main cases + rebalancing via **borrow** or **merge** |
| **Borrow** | If a sibling has extra keys, rotate through the parent |
| **Merge** | If no sibling can lend, merge node with sibling + parent key |

### Delete Cases
```
Case 1: Key in leaf → remove directly
Case 2a: Key in internal node, left child has ≥ t keys → replace with predecessor
Case 2b: Key in internal node, right child has ≥ t keys → replace with successor
Case 2c: Both children have t-1 keys → merge, then delete from merged node
Case 3: Key not in node → ensure child has ≥ t keys (fill), then recurse
```

## Why Databases Prefer B-Trees

| Factor | Red-Black Tree | B-Tree |
|--------|---------------|--------|
| **Fan-out** | 2 children | Up to 2t children |
| **Tree height** | O(log₂ n) | O(log_t n) — much shorter! |
| **Disk I/Os** | One page per level | One page per level (but fewer levels) |
| **Cache locality** | Poor (pointer chasing) | Excellent (keys in contiguous array) |
| **Use case** | In-memory (std::map) | On-disk indexes |

Example: For 1 million keys with t=100:
- RB-Tree height: ~20 levels = ~20 disk reads
- B-Tree height: ~3 levels = ~3 disk reads

## Building and Running

```bash
make        # compile
make run    # compile and run
make clean  # cleanup
```

## Files

| File | Description |
|------|-------------|
| `rbtree.h` | Complete Red-Black Tree (insert, delete, rotations, fixups, verify) |
| `btree.h` | Complete B-Tree (insert, split, delete, borrow, merge, verify) |
| `main.cpp` | Driver program with comprehensive tests |
| `Makefile` | Build targets |
