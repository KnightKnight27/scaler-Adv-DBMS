# Lab 6 - B-Tree Implementation

Name: Aparna Singha  
Roll Number: 24BCS10353

## Aim of the lab

The aim of this lab is to implement a B-Tree in C++ and understand how a balanced multi-way search tree stores and organizes data efficiently. The program supports insertion, searching, sorted traversal, level-wise display, and validation of B-Tree rules.

## What is a B-Tree?

A B-Tree is a balanced search tree where one node can store multiple keys instead of only one key. Since each node can contain many values and multiple child links, the height of the tree remains small even after inserting many elements.

Because the tree height stays low, searching and insertion can be done efficiently.

## Why B-Trees are useful in databases

Databases and file systems usually store data in blocks or pages. A B-Tree works well with this idea because each node can hold several keys at once, similar to how a page stores many records or index entries.

This reduces the number of page accesses needed during search and insertion. Because of this, B-Trees and similar structures are commonly used in database indexes and storage systems.

## Important B-Tree properties

For a B-Tree with minimum degree `t`, these rules must be followed:

- A node can contain at most `2t - 1` keys.
- Every non-root node must contain at least `t - 1` keys.
- If an internal node has `k` keys, it must have `k + 1` children.
- Keys inside each node must be stored in sorted order.
- All leaf nodes must be present at the same depth.
- Values inside child subtrees must stay within the correct range based on the parent keys.

## Insertion approach used

This program uses top-down insertion.

In this method, before going down into a child node, the program checks whether that child is full. If the child is full, it is split first. This prevents overflow at the lower level and avoids the need to move back upward after insertion.

The insertion process is:

1. If the tree is empty, create a root node and insert the first value.
2. If the root is full, create a new root and split the old root.
3. While moving down the tree, split any full child before entering it.
4. Insert the new value into the correct leaf position.
5. Duplicate values are ignored so the tree stores unique keys.

## Features implemented

The program includes:

- Integer insertion in a B-Tree.
- Searching for a key.
- Printing all values in sorted order.
- Displaying the tree level by level.
- Validating B-Tree rules after insertion.
- Handling root splitting and child splitting.
- Cleaning dynamically allocated memory using destructors.

## Files included

```text
lab6/
├── btree.cpp
└── README.md