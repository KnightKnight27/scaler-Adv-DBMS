# B-Tree Implementation in C++

## Overview

This project implements a B-Tree data structure in C++.

A B-Tree is a self-balancing multi-way search tree commonly used in:

- Databases
- File systems
- Indexing systems

This implementation supports:

1. Insertion
2. Search
3. Inorder Traversal

---

## Features

- Dynamic node splitting
- Balanced tree maintenance
- Efficient searching
- Sorted traversal output

---

## B-Tree Properties

For a B-Tree of minimum degree `t`:

- Every node can contain at most `2t - 1` keys.
- Every internal node can have at most `2t` children.
- All leaf nodes are at the same level.
- Keys inside each node are stored in sorted order.

---

## Files

 - btree.cpp
 - README.md


---

## Compilation

Using g++:

```bash
g++ btree.cpp -o btree 
```
--- 

## Execution

```bash
./btree
```

---

## Sample Output

* Inorder Traversal: 5 6 7 10 12 17 20 30
* 12 found in the B-Tree.

---

## Time Complexity

| Operation | Complexity |
| --------- | ---------- |
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Traversal | O(n)       |

---

## Example

### Inserted Keys

```text
10, 20, 5, 6, 12, 30, 7, 17
```

### Traversal Result

```text
5 6 7 10 12 17 20 30
```

---

## Applications

* Database Indexing
* File Systems
* Search Engines
* Storage Systems
* Large-Scale Data Retrieval

---

## Author

C++ B-Tree implementation for educational and academic purposes.

---

## Notes

This implementation follows the standard B-Tree insertion algorithm from the classic algorithms literature and is suitable for data structures coursework and demonstrations.

