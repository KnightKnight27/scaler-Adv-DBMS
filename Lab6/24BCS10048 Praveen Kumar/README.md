# B-Tree (CLRS Chapter 18)

A C++ implementation of the **B-tree** with minimum degree t. Supports insert, search, remove, and tree display. Used as the foundation for database indexes in SQLite, InnoDB, and PostgreSQL.

## What It Does

1. **Inserts** keys using preemptive splitting -- nodes are split on the way down, so no upward traversal is needed
2. **Searches** in O(log n) descending from the root
3. **Removes** keys handling all three CLRS cases: leaf deletion, internal node replacement, and child fill-up before recursion
4. **Displays** in-order (sorted) and level-order (tree structure)

## Quick Start

```bash
# Build
make

# Run
make run
```

Or manually:

```bash
g++ -std=c++17 -O2 -o btree btree.cpp
./btree
```

## B-Tree Node Invariants (t = minimum degree)

| Property | Rule |
|----------|------|
| Keys per node | t-1 to 2t-1 (root: 1 to 2t-1) |
| Children per internal node | t to 2t (root: 2 to 2t) |
| All leaves | Same depth |
| Key ordering | Subtree[i].max < key[i] < subtree[i+1].min |

## Documentation

See [Assignment.md](Assignment.md) for the full writeup: invariants, split/merge/borrow diagrams, all delete cases, complexity, and DBMS relevance.

Praveen Kumar
24bcs10048

## Requirements

- g++ with C++17 support
