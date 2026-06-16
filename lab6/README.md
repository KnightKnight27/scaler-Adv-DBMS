# Lab 6 — B-Tree Implementation in C++

**Name:** Shaurya Verma
**Roll Number:** 24BCS10151

## Overview

This lab implements a **B-Tree**, a self-balancing tree data structure that keeps data sorted and allows searches, insertions, and deletions in logarithmic time. Unlike binary trees, each B-Tree node can hold multiple keys and have multiple children, which makes it especially well-suited for systems that read and write large blocks of data — most notably **databases and file systems**.

SQLite (which we explored in Lab 4) uses B-Trees internally to organize both table data and indexes.

---

## B-Tree Properties

A B-Tree of **minimum degree `t`** obeys these rules:

| Property | Constraint |
|----------|------------|
| Keys per node | Between `t-1` and `2t-1` (root may have fewer) |
| Children per internal node | Exactly `k+1` if the node has `k` keys |
| Leaf depth | All leaves are at the **same level** |
| Key ordering | Keys within a node are sorted; keys in child subtrees respect parent boundaries |

For example, with `t = 3`, each node holds 2–5 keys, and internal nodes have 3–6 children.

---

## Insertion Strategy: Proactive Split

There are two common approaches to B-Tree insertion:

1. **Reactive (bottom-up):** Insert at a leaf, then split upward if it overflows.
2. **Proactive (top-down):** On the way down, split any full node *before* descending into it.

This implementation uses the **proactive (top-down)** strategy. The advantage is that insertion completes in a single root-to-leaf pass — we never need to backtrack up the tree. If the root itself is full, we create a new root first, which is the only time the tree grows taller.

### Step-by-Step

1. If the tree is empty, create a leaf root with the new key.
2. If the root is full (has `2t-1` keys), create a new empty root, make the old root its child, and split it.
3. Walk down the tree. At each level, if the target child is full, split it before descending.
4. Once we reach a leaf, insert the key in sorted position.

---

## Compilation & Running

```bash
cd lab6
g++ -o btree btree.cpp
./btree
```

### Expected Output

The program runs three demos:

**Demo 1** — Inserts 10 keys into a tree with `t=3`, then prints the tree structure, sorted traversal, and search results.

**Demo 2** — Sequential insertion of 1–20 into a `t=2` tree (also known as a 2-3-4 tree), showing how the tree splits and grows.

**Demo 3** — Stress test: inserts 100,000 keys into a `t=50` tree, verifies all invariants hold, and does spot-check searches.

All demos run an invariant checker that validates key count bounds, sort order, leaf depth uniformity, and child count correctness.

---

## Code Structure

```
lab6/
├── btree.cpp     # Full B-Tree implementation with demos
└── README.md     # This file
```

### Key Components

| Component | Description |
|-----------|-------------|
| `BTreeNode` | Stores keys, child pointers, and a leaf flag |
| `splitFullChild()` | Splits a full child node — moves the median up to the parent and creates a new sibling |
| `insertIntoNonFull()` | Recursive insert that proactively splits full children on the way down |
| `findKey()` | Recursive search — walks through keys at each node to pick the right child |
| `printTree()` | Indented tree visualization showing the hierarchy |
| `verify()` | Recursively checks all B-Tree invariants (key bounds, child counts, leaf depths, sort order) |

---

## Complexity

| Operation | Time Complexity |
|-----------|----------------|
| Search    | O(t · log_t(n)) |
| Insert    | O(t · log_t(n)) |
| Split     | O(t) |

Where `n` is the total number of keys. The height of a B-Tree with `n` keys is at most `log_t((n+1)/2)`, making operations very efficient for large `t` values.

---

## Connection to Lab 4 (SQLite)

In Lab 4 we examined SQLite's hex dump and saw how it uses **B-Tree pages** internally:
- Page flag `0x05` for interior B-Tree nodes (similar to internal nodes here)
- Page flag `0x0d` for leaf B-Tree nodes
- Cell pointer arrays that reference child pages (analogous to our `children` vector)

This lab implements the same core data structure that SQLite relies on for organizing table rows and index entries.
