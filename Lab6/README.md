# Lab 6 - B-Tree Index

## Student Info
- **ID:** 24BCS10031
- **Name:** Prabal Patra

---

## Overview

Implemented B-Tree index structure in C++ with template-based generic design. Stores key-value pairs with balanced multi-way branching.

---

## B-Tree Properties

1. All keys in node sorted in order
2. Every non-leaf node has child pointers
3. All leaves exist at same depth
4. Node contains [t-1, 2t-1] keys (where t = degree)
5. Tree remains balanced after insertions

### Capacity

For degree `t`:
- Min keys per node: `t - 1` (except root)
- Max keys per node: `2t - 1`
- Min children: `t`
- Max children: `2t`

---

## Implementation

### Methods

| Method | Description |
|--------|-------------|
| `insert(K key, V value)` | Insert key-value pair, split nodes when full |
| `find(K key)` | Search for key, return optional value |
| `printTree()` | BFS traversal showing levels |
| `getAccessCount()` | Number of nodes accessed in last search |
| `getHeight()` | Tree height |

### Split Operation

When node has 2t-1 keys:
1. Median key (at index t-1) promoted to parent
2. Left half stays in current node (t-1 keys)
3. Right half moves to new sibling node (t-1 keys)
4. Children redistributed if non-leaf

---

## Build & Run

```bash
g++ -std=c++17 btree.cpp -o btree
./btree
```

---

## Sample Output

```
Degree: 3, Max keys per node: 5, Min keys (non-root): 2

Tree structure:
Level 0: [30:thirty, 60:sixty] (leaf=no, keys=2)
Level 1: [10:ten, 20:twenty] (leaf=yes, keys=2)
Level 1: [40:forty, 50:fifty] (leaf=yes, keys=2)
Level 1: [70:seventy, 80:eighty, 90:ninety, 100:hundred, 110:hundred10] (leaf=yes, keys=5)

Search:
  Find 50: FOUND 'fifty' (accessed 2 nodes)
  Find 25: NOT FOUND (accessed 2 nodes)
```

---

## Time Complexity

| Operation | Complexity |
|-----------|------------|
| Search | O(log n) |
| Insert | O(log n) |
| Traversal | O(n) |
| Split | O(t) |

---

## Database Indexing Concepts

B-Trees used in databases because:
- Single node access = one disk read
- High fanout = low tree height = few disk reads
- Self-balancing = consistent performance
- Sorted keys = range queries efficient
- Sequential access = sequential disk I/O

This lab prepares for B+ tree which is more common in real DBMS.