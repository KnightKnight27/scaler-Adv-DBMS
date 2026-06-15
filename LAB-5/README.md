# Lab 5 - Red Black Tree Implementation

## Student Details
- Name: Dhruv Bansal
- Roll Number: 24BCS10114

## Objective
Implement insertion in a Red Black Tree and confirm that the tree stays balanced after each insertion.

## Files
- `red_black_tree.cpp`: Complete C++17 implementation with insertion, rotations, search, inorder traversal, level-order traversal, and validity checks.

## Compile and Run

```powershell
g++ -std=c++17 lab5/red_black_tree.cpp -o lab5/red_black_tree
.\lab5\red_black_tree.exe
```

## Red Black Tree Rules Used
- Every node is either red or black.
- The root is always black.
- Red nodes cannot have red children.
- Every path from a node to a null leaf has the same number of black nodes.
- The tree also preserves the binary search tree ordering rule.

## Notes
The insertion begins like a normal binary search tree insertion. If adding a red node breaks the red-black rules, the fix-up step uses recoloring and rotations to restore balance.
