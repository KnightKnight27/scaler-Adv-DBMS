# Lab 6: B-Tree Index Implementation

**Student:** Lokendra Singh Rajawat — 23bcs10075  
**Subject:** Advanced Database Management Systems

---

## Objective

To implement and analyze a B-Tree Index — the foundational data structure used in database systems like MySQL (InnoDB), PostgreSQL, SQLite, and most file systems to support O(log n) search, insert, and range queries on large datasets stored across many disk pages.

---

## Theory

### B-Tree Properties (Minimum Degree t)

| Property | Rule |
|----------|------|
| Key capacity | Every non-root node has **t-1 ≤ keys ≤ 2t-1** |
| Child capacity | Every non-leaf node with k keys has exactly **k+1 children** |
| Sorted keys | Keys within each node are in **ascending order** |
| Leaf depth | All **leaf nodes are at the same depth** (perfectly balanced) |
| Split policy | A **full node (2t-1 keys)** is split before descending into it |

With **t=3** (as used in this implementation):
- Minimum keys per node: **2**
- Maximum keys per node: **5**
- Maximum children per node: **6**

### Why B-Trees in Databases?

Unlike binary trees, B-Trees have **high fanout** — each node can hold many keys. This minimizes the tree height and therefore the number of disk page reads needed to find a record:

| Structure | 1M records, height | Disk reads |
|-----------|-------------------|------------|
| Binary BST | ~20 levels | ~20 I/Os |
| B-Tree (t=100) | ~3 levels | ~3 I/Os |

---

## How to Compile and Run

```bash
cd lab6
make        # compiles btree.c → ./btree
make run    # compile + run
make clean  # remove binary
```

---

## Implementation Design

### Split-on-the-Way-Down Strategy

Instead of inserting first and splitting on the way up, we split **full nodes as we descend**. This ensures we always have room to insert without needing to walk back up the tree.

```
Insert(key):
  if root is full → split root, tree height increases
  descend from root:
    before entering a child, if child is full → split it first
  insert into the leaf (guaranteed non-full)
```

### Node Splitting

When a full node (5 keys for t=3) is encountered:

```
Before:         [1, 3, 5, 6, 7]

Split at median (index t-1 = 2, key=5):
  Left:   [1, 3]        (t-1 = 2 keys)
  Median: 5   → promoted to parent
  Right:  [6, 7]        (t-1 = 2 keys)

Parent becomes: [..., 5, ...]
                 children: [left_half] [right_half]
```

---

## Sample Output

### Initialization
```
[Init] B-Tree created. Degree t=3
       Min keys per node : 2
       Max keys per node : 5
       Max children/node : 6
```

### Insertion + Split Example
```
--- Inserting key=30, value="Frank" ---
[Split-Root] Root is full. Creating new root and splitting.
[Split] Node full (5 keys). Promoting median key=10 to parent.
       Left child: 2 keys | Right child: 2 keys
       Inserted key=30 into leaf node. Node now has 3 keys.
```

### Final Tree Structure (20 records inserted)
```
--- B-Tree Structure (Level Order) ---
    Degree t=3 | Height=1 | Nodes=5 | Splits=3

Level 0: [10,20,35]
Level 1: [1,3,5,6,7] [11,12,15,17] [22,25,28,30,32] [40,45,50]
```

### Search Operations
```
[Search] Key 15 FOUND — value: "Laura"  | Node accesses: 2
[Search] Key 40 FOUND — value: "Niaj"   | Node accesses: 2
[Search] Key 99 NOT FOUND               | Node accesses: 2
[Search] Key 28 FOUND — value: "Quinn"  | Node accesses: 2
```

### Property Verification
```
[PASS] All keys sorted within nodes.
[PASS] All keys satisfy BST ordering.
[PASS] All leaf nodes at depth 1 (uniform).
[PASS] Node key counts within [2, 5].
```

---

## Observations

| Metric | Value |
|--------|-------|
| Records inserted | 20 |
| Total node splits | 3 |
| Tree height | 1 (2 levels) |
| Total nodes | 5 |
| Max node accesses to find any key | 2 |
| Binary tree equivalent height | Up to 20 levels |

### Key Observations

1. **Efficient height**: 20 records in only 2 levels — far shallower than a BST.
2. **Proactive splits**: Full nodes are split before descending, so insertion never needs a second pass up the tree.
3. **Sorted storage**: All keys within nodes are in ascending order at all times.
4. **Uniform leaf depth**: All leaf nodes are at depth 1 — confirming the balanced property.
5. **O(2) accesses**: For 20 records with t=3, every search completes in exactly 2 node accesses (root + leaf).
6. **Node reuse**: After a root split, the old root becomes a child and a new root is created — tree height grows by exactly 1.

---

## B-Tree vs Other Index Structures

| Feature | Hash Index | Binary BST | B-Tree |
|---------|-----------|------------|--------|
| Search | O(1) avg | O(log n) avg | O(log n) |
| Range queries | ❌ Not supported | ✅ | ✅ Optimal |
| Disk-friendly | ❌ (scattered) | ❌ | ✅ (high fanout) |
| Balanced | ❌ | Only with AVL/RB | ✅ Always |
| Used in | Redis, Memcached | In-memory only | MySQL, PostgreSQL, SQLite |

---

## Relation to Database Indexing

- SQLite uses a **B-tree** (specifically B+tree variant) for both table storage and indexes.
- PostgreSQL uses B-tree for most indexes, with the leaf level storing tuple pointers.
- The **root page** of an SQLite table corresponds directly to the root node in our B-Tree.
- **Node splits** in this implementation mirror how SQLite and PostgreSQL handle page splits when a page becomes too full.

---

## Learning Outcomes

After completing this lab, I was able to:
- Understand B-Tree properties and how they guarantee O(log n) height.
- Implement the split-on-the-way-down insertion strategy.
- Observe node splitting in action and understand median key promotion.
- Implement BFS-level-order tree printing for structural analysis.
- Verify all B-Tree properties programmatically.
- Connect B-Tree mechanics directly to database index concepts (pages, splits, fanout).
