# Lab 5 - Red Black Tree Implementation

## Student Details
- Name: Swaim Sahay
- Roll Number: 24BCS10335

## Aim
To implement a Red Black Tree in C++ and show that insertions keep the tree balanced while preserving binary search tree ordering.

## Submitted Files
- `red_black_tree.cpp`: C++17 source file containing insertion, rotations, search, inorder display, level-order display, and a validation routine.

## Build Command

```powershell
g++ -std=c++17 lab5/red_black_tree.cpp -o lab5/red_black_tree
.\lab5\red_black_tree.exe
```

## Properties Checked
- The root node is black.
- No red node has a red parent or red child.
- Left subtree keys are smaller and right subtree keys are larger.
- Every root-to-null path has the same black height.

## Summary
The program inserts a sequence of keys into the tree, applies recoloring and rotations whenever required, and prints whether the tree is valid after every insertion.
