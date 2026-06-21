# Lab 4 - Part 2: Full B-Tree Implementation in C++

## Objective

Implement a B-Tree supporting search, insertion, splitting, borrowing, merging, and deletion operations.

## Features Implemented

- Search
- Insert
- Split Child
- Borrow from Previous Sibling
- Borrow from Next Sibling
- Merge Nodes
- Delete Operation
- Tree Traversal

## B-Tree Overview

A B-Tree is a self-balancing multi-way search tree optimized for storage systems and databases.

For minimum degree `t`:

- Every node contains between `t-1` and `2t-1` keys.
- Root may contain fewer keys.
- All leaves are at the same level.
- Height remains O(log n).

## Algorithms Used

### Search
Navigate to the correct child based on key comparisons.

### Insert
- Insert key into a non-full node.
- Split nodes when they become full.

### Split Child
- Move middle key to parent.
- Create a new sibling node.
- Redistribute keys and children.

### Borrow Operation
When a node underflows:
- Borrow key from left sibling, or
- Borrow key from right sibling.

### Merge Operation
If borrowing is impossible:
- Merge two sibling nodes.
- Pull separator key from parent.

### Delete
Deletion handles:
- Leaf node deletion
- Internal node deletion
- Borrowing
- Merging
- Recursive rebalancing

## Compilation

```bash
g++ -std=c++17 btree.cpp -o btree
```

## Execution

```bash
./btree
```

## Example Operations

Inserted Keys:

```
10 20 5 6 12 30 7 17
```

Operations Demonstrated:

- Insert
- Split
- Delete
- Borrow
- Merge

## Time Complexity

| Operation | Complexity |
|------------|------------|
| Search | O(log n) |
| Insert | O(log n) |
| Delete | O(log n) |
| Split | O(t) |
| Merge | O(t) |

## Applications

- MySQL InnoDB Indexes
- PostgreSQL Indexes
- File Systems
- Key-Value Stores
- Database Storage Engines

## Advantages

- High fan-out reduces tree height.
- Minimizes disk I/O.
- Efficient for large datasets.
- Ideal for database indexing.

## Conclusion

B-Trees are widely used in database systems because they keep data balanced while minimizing disk accesses. Split, borrow, and merge operations ensure efficient maintenance of the tree structure.