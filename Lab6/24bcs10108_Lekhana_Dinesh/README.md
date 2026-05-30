# Lab 6 - B-Tree Implementation in C++

Name: Lekhana Dinesh
Roll Number: 24BCS10108

## Objective

Implement a B-Tree data structure in C++ and demonstrate insertion, search, inorder traversal, and level-order display of the tree.

## B-Tree Explanation

A B-Tree is a balanced tree designed for database indexing and disk-based storage. It maintains sorted keys in each node and ensures that all leaves remain at the same depth.

## B-Tree Properties

- Maximum keys in a node: 2t - 1
- Maximum children in a node: 2t
- All leaves remain at the same level
- Keys inside each node are sorted

## Features Implemented

- Insert integer keys into a B-Tree
- Search for existing and missing keys
- Inorder traversal of all keys
- Level-order display of node structure
- Duplicate key handling with clear notification
- Configurable minimum degree through constructor

## File Structure

- `btree.cpp` - B-Tree implementation and demonstration program
- `README.md` - documentation, compile/run instructions, and expected output

## Compile and Run

Direct compile command for Windows / PowerShell:

```powershell
g++ -std=c++17 -Wall -Wextra btree.cpp -o btree
.\btree.exe
```

## Sample Output

```text
Inserting keys: 12 5 18 3 7 15 20 30 10 8 25

Inorder traversal: 3 5 7 8 10 12 15 18 20 25 30

B-Tree level-order structure:
Level 0: [7,18]
Level 1: [3,5] [8,10,12,15] [20,25,30]

Search for 15: Found
Search for 99: Not found
```

## Complexity Analysis

- Search: O(log n)
- Insert: O(log n)
- Traversal: O(n)

## Learning Outcome

This lab demonstrates how B-Trees support DBMS indexing by keeping nodes balanced and reducing tree height. B-Trees are useful for page-oriented storage since each node can hold multiple keys and children, minimizing disk accesses and enabling efficient range queries.
