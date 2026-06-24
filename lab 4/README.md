# Lab Session 4: Red-Black Tree & Full B-Tree in C++

## Objective
1. Implement a Red-Black Tree (self-balancing BST used in database index structures) in C++.
2. Implement a full B-Tree (an on-disk index structure used by databases) supporting insert, merge (split promotion), and delete with underflow borrowing and merging.

---

## Files

| File | Description |
|------|-------------|
| [rbt.cpp](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%204/rbt.cpp) | Red-Black Tree implementation with node color adjustment, rotations, insertion, deletion, and inorder print. |
| [btree.cpp](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%204/btree.cpp) | B-Tree ($T=2$) implementation including node splitting, sibling borrowing, node merging, search, insertion, deletion, and inorder print. |
| [Makefile](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%204/Makefile) | Makefile containing build rules and test run options. |

---

## Structural Comparison

| Property           | Red-Black Tree                        | B-Tree (order t)                          |
|--------------------|---------------------------------------|-------------------------------------------|
| **Storage**        | In-memory                             | Designed for disk (large node = 1 page)   |
| **Node size**      | 1 key per node                        | Up to `2t-1` keys per node                |
| **Height**         | $O(\log n)$                           | $O(\log_t n)$ — much shorter for large $t$|
| **Database Use**   | In-memory indexes, `std::map`          | On-disk indexes (PostgreSQL, MySQL, SQLite)|
| **Cache Behavior** | Poor (pointer chasing)                | Excellent (sequential keys in one node)   |
| **Rebalancing**    | Rotations and color changes           | Node split, borrow, and merge operations  |

---

## Compilation and Execution

Use the helper `Makefile` to compile and run:

```bash
# Compile both targets
make

# Run Red-Black Tree demo
make run_rbt

# Run B-Tree demo
make run_btree

# Remove binaries
make clean
```
