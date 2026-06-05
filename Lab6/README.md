# Lab 6: B-Tree Index Implementation

## Objective

To implement a B-Tree index that stores key-value pairs, supports fast search, and automatically maintains balance by splitting full nodes during insertion.

---

## B-Tree Properties

For a B-Tree of degree `t`:

| Property | Rule |
|----------|------|
| Max keys per node | `2t - 1` |
| Min keys per node (non-root) | `t - 1` |
| Max children per node | `2t` |
| All leaf nodes | at same depth |
| Keys within a node | sorted order |

---

## What the Code Does

### Task 1 — Initialization
Creates a B-Tree with degree `t=3` (max 5 keys per node, min 2). Prints node capacity limits.

### Task 2 & 3 — Insertion + Node Splitting
Inserts 12 key-value pairs. When a node hits `2t-1` keys, a split happens:
- The **median key** is promoted up to the parent
- Keys to the left of median stay in the original node
- Keys to the right go into a new sibling node

If the **root** is full, a new root is created — this is the only way tree height increases.

### Task 4 — Search
Searches traverse from root downward, skipping entire subtrees based on key comparisons. Prints the value found and number of node accesses. With only 12 records and depth 1, every search takes just 2 node accesses.

### Task 5 — Tree Structure + Stats
Prints the tree with indentation showing parent-child relationships and labels leaf nodes. Also prints depth, node count, and key count.

### Task 6 — Inorder Traversal
Visits all keys in sorted order, confirming the BST ordering property is maintained across all nodes.

---

## How to Run

```bash
g++ -std=c++17 -Wall -o b_tree b_tree.cpp
./b_tree
```

---

## Sample Output

```
>>> Task 2 & 3: Insertions + Node Splitting

  [split] root is full, splitting root
  [split] node with keys [5,6,10,12,20] → promote key=10

  [split] child at index 1 is full
  [split] node with keys [12,17,20,25,30] → promote key=20

>>> Task 5: Tree Structure

  B-Tree structure (degree t=3):
[10|20]
    [3|5|6|7|8] (leaf)
    [12|17] (leaf)
    [25|30|35] (leaf)

>>> Task 6: Inorder Traversal
  Traversal: 3 5 6 7 8 10 12 17 20 25 30 35

>>> Task 4: Search Operations
  [search] key=6  FOUND val="Diana" | node accesses: 2
  [search] key=25 FOUND val="Karl"  | node accesses: 2
  [search] key=99 NOT FOUND         | node accesses: 2
```

---

## Observations

- Inserting just 12 records caused 2 splits — in a real database with millions of rows, splits happen infrequently relative to total insertions.
- All leaf nodes sit at depth 1 after 12 insertions, confirming the tree stays balanced.
- Every search took exactly 2 node accesses regardless of which key was searched — this is the power of B-Tree indexing.
- Inorder traversal outputs keys in sorted order, which is why B-Trees support efficient range queries in databases.
- In a real DB, each node corresponds to a **disk page** (typically 4KB or 16KB), so fewer node accesses = fewer disk reads.

---

## Complexity

| Operation | Complexity |
|-----------|------------|
| Insert    | O(log n)   |
| Search    | O(log n)   |
| Traversal | O(n)       |
| Split     | O(t)       |

---

## Author
Submitted as part of Lab 6 – Database Systems Lab  
Date: May 2026