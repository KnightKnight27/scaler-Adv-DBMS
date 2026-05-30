# Lab 6 - B-Tree

## Student Details
- Name: Harshit Tiwari
- Roll Number: 24BCS10277

## Aim
Implement a B-Tree in C++ and demonstrate insertion, search, sorted traversal, and level-wise structure after node splits.

## Files
- `btree.cpp`: C++17 implementation of a B-Tree with minimum degree 3.

## Compile And Run

```powershell
g++ -std=c++17 lab6/btree.cpp -o lab6/btree
.\lab6\btree.exe
```

## Operations Implemented
- Insert a key using top-down splitting.
- Search for existing and missing keys.
- Print keys in sorted order using inorder traversal.
- Print the tree level by level to show node splits.
- Validate ordering and key-count rules after all insertions.

## Notes
A B-Tree keeps multiple sorted keys inside each node. When a node becomes full, the middle key is moved to the parent and the remaining keys are split into two child nodes. This keeps the tree shallow, which is useful for database indexes and disk-page based storage.
