# Lab 6: B-Tree Index Implementation

**Student:** Talin Daga (24bcs10321)

## Objective
Implement a B-Tree index structure (minimum degree t = 3) that stores key-value pairs, maintains sorted order, splits full nodes proactively on insertion, and demonstrates how database systems use B-trees for efficient indexing.

## Files

| File | Description |
|------|-------------|
| `btree.c` | Complete C implementation — 6 tasks, split/search logging |

## Build & Run

```bash
gcc -Wall -Wextra -o btree btree.c
./btree
```

---

## B-Tree Configuration (t = 3)

| Parameter | Formula | Value |
|-----------|---------|-------|
| Minimum degree | t | 3 |
| Max keys / node | 2t − 1 | **5** |
| Min keys / non-root node | t − 1 | **2** |
| Max children / node | 2t | **6** |

---

## B-Tree Invariants Maintained

| # | Invariant |
|---|-----------|
| I1 | Keys within every node are stored in **sorted order** |
| I2 | All leaf nodes exist at the **same depth** |
| I3 | Every non-leaf node with *n* keys has exactly *n+1* children |
| I4 | Each non-root node holds between **t−1** and **2t−1** keys |
| I5 | Root holds between **1** and **2t−1** keys (unless empty) |

---

## Tasks Demonstrated

| Task | Description |
|------|-------------|
| 1 | Init — tree created (t=3), all invariants and capacity limits stated |
| 2 | Insert 10, 20, 5, 6, 12 — fills root to capacity with sorted insertion log |
| 3 | Insert 30, 7, 17, 3, 25, 35, 40, 2, 45, 50 — 3 splits logged with median, left/right halves |
| 4 | Search 17, 25 (found); 11 (absent); 50 (found) — node-access path at each level |
| 5 | Indented tree, level-order layout, and statistics (nodes, keys, height, avg keys) |
| 6 | Indexing analysis — ordered storage, disk I/O reduction, range queries, real-world use |

---

## Final Tree (after 15 insertions)

```
  Level 0: [10, 20, 35]
  Level 1: [2,3,5,6,7]  [12,17]  [25,30]  [40,45,50]

  Stats: 5 nodes, 15 keys, height=1, 3 splits, avg 3.0 keys/node
```

---

## Split Summary

| Split # | Trigger | Full node | Median promoted | Left half | Right half |
|---------|---------|-----------|-----------------|-----------|------------|
| 1 | Insert 30 | [5,6,10,12,20] (root) | **10** | [5,6] | [12,20] |
| 2 | Insert 35 | [12,17,20,25,30] | **20** | [12,17] | [25,30] |
| 3 | Insert 50 | [25,30,35,40,45] | **35** | [25,30] | [40,45] |

---

## Observations (fill in after running)

| Metric | Observed Value |
|--------|---------------|
| Root after batch 1 (5 insertions) | |
| Key that triggered split #1 | |
| Median promoted in split #1 | |
| Key that triggered split #2 | |
| Root after all 15 insertions | |
| Tree height | |
| Total nodes | |
| Total splits | |
| Node accesses to find key 17 | |
| Node accesses to find key 11 (absent) | |
| Leaf node with max capacity | |

---

## Analysis Questions

1. **Why does a B-tree grow upward (from the root) rather than downward?**

2. **What is the median key and why is it chosen for promotion during a split?**

3. **Why does the CLRS proactive-split strategy avoid travelling back up the tree?**

4. **How does storing multiple keys per node reduce disk I/O in database systems?**

5. **What is the difference between a B-tree and a B+-tree? Which do databases prefer?**

6. **How does the B-tree guarantee all leaves remain at the same depth?**

7. **Compare B-tree search cost to an unbalanced BST for 15 keys.**

8. **How would you implement a range query [lo, hi] on this B-tree?**

---

## Complexity Reference

| Operation | B-Tree | Unbalanced BST (worst) |
|-----------|--------|----------------------|
| Search | O(log_t n) | O(n) |
| Insert | O(log_t n) | O(n) |
| Split cost (amortised) | O(1) per insert | — |
| Node accesses (height h) | h + 1 | n |

> For t = 3 and n = 15: height ≤ log₃(15) ≈ 2.5 → at most **3 levels**,  
> vs. up to **15 comparisons** in a worst-case BST.
