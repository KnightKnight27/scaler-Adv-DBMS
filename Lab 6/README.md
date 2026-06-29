# Lab 6: B-Tree Index Implementation

**Roll No.:** 24BCS10318
**Name:** Utkarsh Raj

## Objective
The objective of this lab is to understand and implement a B-Tree Index, one of the most widely used indexing structures in database management systems. Students will learn how B-Trees organize data efficiently, support fast searching, and maintain balance during insertion operations.

## Description
In this experiment, a B-Tree index is implemented to store key-value pairs and support efficient data retrieval. The tree automatically maintains a balanced structure by splitting nodes when they become full, ensuring that search and insertion operations remain efficient even as the dataset grows.

B-Trees are extensively used in database systems, file systems, and storage engines because they minimize disk accesses and provide logarithmic-time operations. Unlike binary search trees, B-Trees can store multiple keys within a single node, reducing tree height and improving performance for large datasets.

The implementation demonstrates how records are inserted into the tree, how nodes are split when capacity limits are exceeded, and how searches are performed by traversing the appropriate branches of the tree.

---

## Tasks
1. **B-Tree Initialization**
   - Create a B-Tree with a specified degree and initialize the root node.
   - Observe the tree structure at initialization, node capacity limits, and min/max keys per node.
2. **Record Insertion**
   - Insert multiple key-value pairs into the B-Tree.
   - Observe placement of records within nodes, ordering of keys, and overall growth.
3. **Node Splitting**
   - When a node becomes full, perform a split operation.
   - Record the median key selected for promotion, creation of new child nodes, redistribution of records, and changes in tree height.
4. **Search Operations**
   - Perform searches for existing and non-existing keys.
   - Observe traversal path followed, number of node accesses, and search efficiency.
5. **Tree Structure Analysis**
   - Print and analyze the structure of the B-Tree after multiple insertions.
   - Observe key distribution among nodes, parent-child relationships, and tree depth.
6. **Indexing Behavior**
   - Analyze how the B-Tree acts as an index structure (reduced search space, ordered storage).

---

## B-Tree Properties
- All keys within a node are stored in sorted order.
- Every non-leaf node contains child pointers.
- All leaf nodes exist at the same depth.
- Nodes contain a limited range of keys based on the tree degree.
- The tree remains balanced after insertions.
- Search, insertion, and traversal operations are performed efficiently.

---

## How to Build and Run

### Prerequisites
- CMake 3.10+
- A C++ compiler supporting C++17 (e.g., GCC, Clang, MSVC)

### Build Instructions
Run the following commands in the directory of Lab 6:
```powershell
cmake -B build -S .
cmake --build build
```

### Execution
Run the compiled executable:
```powershell
.\build\BTreeLab.exe
```
