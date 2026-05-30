# Lab 6 — B-Tree Implementation

## Student Details

* Name: Sarthak Arora
* Roll No: 24BCS10150

---

## Overview

This project implements a generic B-Tree data structure in C++ using templates.

A B-Tree is a self-balancing search tree commonly used in database systems and file systems because it minimizes disk accesses and maintains sorted data efficiently.

The implementation supports:

* Insertion
* Search
* Inorder Traversal
* Tree Visualization
* Duplicate Key Handling

---

## Features

* Generic template implementation
* Configurable minimum degree (t)
* Automatic node splitting
* Sorted key storage
* Efficient search operations
* Inorder traversal output

---

## Complexity

| Operation | Time Complexity |
| --------- | --------------- |
| Search    | O(log n)        |
| Insert    | O(log n)        |
| Traversal | O(n)            |

---

## Build

```bash
g++ -std=c++17 btree.cpp -o btree
./btree
```

---

## Sample Operations

* Insert multiple keys
* Search for existing keys
* Search for missing keys
* Print inorder traversal
* Display tree structure

---

## Conclusion

The project demonstrates the working of a B-Tree and shows how balanced multi-way search trees are used to efficiently manage large datasets.
