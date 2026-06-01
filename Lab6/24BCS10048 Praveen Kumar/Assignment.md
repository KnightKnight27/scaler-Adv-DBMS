# B-Tree Index (CLRS Chapter 18)

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048
**Date:** 2026-05-30

---

## 1. Objective

Implement a B-tree index storing key-value pairs with minimum degree t. The tree must support insert, search, remove, and traversal while maintaining balance after every operation.

---

## 2. Build and Run

```bash
g++ -std=c++17 -O2 -o btree btree.cpp
./btree
```

The program inserts 18 key-value records into a t=3 B-tree, prints the tree structure and in-order display, searches for several keys, deletes entries, and shows the result.

---

## 3. Why B-Trees in Databases

A B-tree with minimum degree t stores up to 2t-1 key-value pairs per node. With t=512 and 8 KB pages each node holds up to 1023 pairs. A table with 1 billion rows needs a tree of depth log_1024(1e9) ~ 3. Any lookup by primary key touches at most 3-4 pages whether the table has 1 million or 1 billion rows.

This is why:
- **SQLite** stores every table as a B-tree where each page is one node (4 KB default).
- **InnoDB** (MySQL) uses a B+ tree as its clustered index; leaf pages store full rows.
- **PostgreSQL** uses a B-tree variant for all B-tree index access methods.

The critical design choice: wide nodes = shallow trees = fewer disk I/O operations per lookup. A disk read fetches a whole page at once, so 1023 keys costs the same I/O as 1.

---

## 4. B-Tree Invariants

The implementation maintains all properties required by CLRS 18.1:

| # | Property |
|---|----------|
| 1 | All keys within a node are stored in sorted order |
| 2 | Every non-leaf node contains child pointers (n+1 children for n keys) |
| 3 | All leaf nodes exist at the same depth |
| 4 | Non-root nodes contain t-1 to 2t-1 keys; root contains 1 to 2t-1 |
| 5 | The tree remains balanced after every insert and delete |
| 6 | Search, insertion, and traversal are O(t * log_t n) |

---

## 5. Task 1 -- B-Tree Initialization

```cpp
BTree bt(3);
```

Creates an empty B-tree with minimum degree t=3. The root starts as an empty leaf node.

- Minimum keys per non-root node: t-1 = **2**
- Maximum keys per node: 2t-1 = **5**
- Minimum children per internal node: t = **3**
- Maximum children per internal node: 2t = **6**

At initialization the root is the only node, and it is a leaf. The tree has depth 0.

---

## 6. Task 2 -- Record Insertion

Records are inserted as key-value pairs. The key determines position (BST ordering); the value is the associated data (in our demo, a book title for each integer key).

Insert uses the **preemptive split** variant (CLRS 18.3):
- Split any full node (2t-1 keys) encountered on the way down.
- This guarantees the parent always has room to receive a promoted median.
- No upward traversal is ever needed.

```
insert_nonfull(x, key, value):
    if x is a leaf:
        insert entry at correct sorted position

    else:
        find child[i] that should contain key
        if child[i] is full:
            split_child(x, i)
            adjust i if needed
        insert_nonfull(child[i], key, value)
```

Keys in each node are always in ascending order. The value travels with its key and is accessible in O(1) once the key is found.

---

## 7. Task 3 -- Node Splitting

A node splits when it reaches 2t-1 = 5 entries (for t=3).

**Split procedure** (`split_child(x, i)`):

```
Before:               After:
  x: [... ki ...]       x: [... ki  MED  ki+1 ...]
           |                      |         |
   y:[k1 k2 k3 k4 k5]         y:[k1 k2]  z:[k4 k5]
      c0 c1 c2 c3 c4 c5
```

1. The median entry `k3` (index t-1) is promoted to the parent `x`.
2. Left half `[k1, k2]` stays in node `y`.
3. Right half `[k4, k5]` moves to new node `z`.
4. If `y` is internal, its children are split the same way: `[c0..c2]` stay in `y`, `[c3..c5]` go to `z`.

When the root splits, a new empty root is created, the old root becomes its child, and the split immediately follows. This is the only time the tree grows taller.

---

## 8. Task 4 -- Search Operations

```
search(x, key):
    find first i where entries[i].key >= key
    if i < n and entries[i].key == key:
        return entries[i].value          (found)
    if x is leaf:
        return null                      (not found)
    return search(children[i], key)      (recurse)
```

Sample output:
```
  search(17) -> found: "Compilers (Dragon Book)"
  search(25) -> found: "Pragmatic Programmer"
  search(1)  -> found: "The C Programming Language"
  search(99) -> not found
```

Each level of the tree eliminates one subtree. The search path length is at most the tree height, which is bounded by log_t(n).

---

## 9. Task 5 -- Tree Structure Analysis

After inserting 18 records with t=3, the tree has depth 2:

```
  Tree structure (level-order):
  [12:"Clean Code", 25:"Pragmatic Programmer"]
      [5:"OSTEP", 7:"Linux...", 10:"Database Internals"]
          [1:"The C...", 3:"SICP"]
          [6:"CLRS"]
          [...]
      [17:"Compilers...", 20:"DDIA", 22:"SRE"]
          [...]
      [35:"CS:APP", 40:"Computer Networks", 45:"Refactoring"]
          [...]
```

Observations:
- **Distribution**: keys spread evenly across nodes through splits; no node has fewer than t-1=2 or more than 2t-1=5 entries.
- **Depth**: all leaves at the same level (depth = 2 for 18 records with t=3).
- **Balance**: the tree is inherently balanced -- every insertion either fills an existing node or causes a split that keeps depths equal.

---

## 10. Task 6 -- Indexing Behavior

The B-tree acts as an ordered index:

1. **Ordered storage**: keys in every node and across nodes are in ascending order, enabling range queries and in-order traversal without any extra sort step.

2. **Efficient navigation**: at each node, a linear scan (or binary search) identifies the correct child pointer in O(t) comparisons. The number of nodes visited is O(log_t n).

3. **Reduced search space**: each level eliminates all but 1/(2t) of the remaining keys. With t=512 (a realistic database page size), the tree has at most 3-4 levels for a billion-row table.

In a real DBMS, each B-tree node corresponds to one disk page. The node size is chosen to match the page size so that one node is fetched per I/O operation. The key insight is that a single I/O that reads 1023 keys costs the same as one that reads 1 key -- so maximizing keys per node minimizes disk accesses.

---

## 11. Delete Operations (All 3 Cases)

### Case 1 -- Key in a leaf
Direct deletion. The leaf may now have t-1 keys (minimum allowed), which is valid.

### Case 2 -- Key in an internal node

| Subcase | Condition | Action |
|---------|-----------|--------|
| 2a | Left child has >= t entries | Replace with predecessor (rightmost of left subtree), delete from there |
| 2b | Right child has >= t entries | Replace with successor (leftmost of right subtree), delete from there |
| 2c | Both children have t-1 entries | Merge key + both children into one node, delete from merged node |

### Case 3 -- Key not in current node, child has t-1 entries

Before recursing into a child with only t-1 entries, fill it up:

| Subcase | Condition | Action |
|---------|-----------|--------|
| Borrow left | Left sibling has >= t entries | Rotate: parent key down to child, sibling's last key up to parent |
| Borrow right | Right sibling has >= t entries | Rotate: parent key down to child, sibling's first key up to parent |
| Merge | Both siblings have t-1 entries | Merge child with one sibling, pull separator key from parent |

---

## 12. Complexity

| Operation | Time | Notes |
|-----------|------|-------|
| search | O(t * log_t n) | Linear scan per node; O(log n) levels |
| insert | O(t * log_t n) | At most one split per level |
| remove | O(t * log_t n) | At most one merge/borrow per level |
| in-order traversal | O(n) | Visits every entry once |
| Space | O(n) | n entries distributed across nodes |

With t set to match page size (e.g., t=512 for 8 KB pages), `log_t(n)` is at most 4 for a billion records.

---

## 13. B-Tree vs B+ Tree

Real database engines typically use a B+ tree variant:
- All data lives in the leaves; internal nodes hold only routing keys.
- Leaf nodes are linked in a doubly-linked list for efficient range scans.
- This allows `BETWEEN`, `ORDER BY`, and `LIMIT` queries to scan leaves sequentially after a single root-to-leaf descent.

Our implementation is a pure B-tree (data in all nodes, no leaf linking), which is sufficient to demonstrate all the core algorithmic concepts.

---

## 14. Files in this Submission

| File | Description |
|------|-------------|
| `btree.cpp` | C++ implementation of the B-tree index |
| `Makefile` | Build instructions |
| `Assignment.md` | This document |
| `README.md` | Quick-start guide |

---

## 15. References

- Cormen, T.H. et al. *Introduction to Algorithms*, 3rd ed., Ch. 18 (B-Trees)
- Comer, D. "The Ubiquitous B-Tree." ACM Computing Surveys, 11(2), 1979.
- SQLite file format: https://www.sqlite.org/fileformat2.html
- PostgreSQL B-tree index: https://www.postgresql.org/docs/current/btree.html
