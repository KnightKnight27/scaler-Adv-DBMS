# B-Tree Implementation in C++

## Objective

To implement a B-Tree data structure in C++ supporting insertion, search, and traversal operations while maintaining balanced tree properties.

## Features

* Dynamic insertion of keys
* Efficient search operation
* Automatic node splitting when a node becomes full
* Balanced multi-way tree structure
* Inorder traversal of stored keys

## Concepts Used

* B-Tree Data Structure
* Recursive Insertion
* Node Splitting
* Balanced Search Trees
* Dynamic Memory Allocation

## Compilation

```bash
g++ btree.cpp -o btree
```

## Execution

```bash
./btree
```

## Operations Supported

1. Insert a key into the B-Tree
2. Search for a key
3. Traverse and display all keys
4. Exit the program

## Time Complexity

| Operation | Complexity |
| --------- | ---------- |
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Traverse  | O(n)       |

## Sample Input

```text
Insert: 10
Insert: 20
Insert: 5
Insert: 6
Insert: 12
```

## Sample Output

```text
Value found.
B-Tree Traversal:
5 6 10 12 20
```

## Applications

* Database Indexing
* File Systems
* Large-Scale Storage Systems
* Search Engines
* Multi-Level Index Structures
