# Lab 5 - Red Black Tree

## Student Details
- Name: Harshit Tiwari
- Roll Number: 24BCS10277

## Objective
Implement insertion in a Red Black Tree and verify that the tree remains balanced after every insert.

## Files
- `red_black_tree.cpp`: Complete C++17 implementation with insertion, rotations, search, inorder traversal, level-order traversal, and property validation.

## Compile And Run

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
The insertion starts like a normal binary search tree insertion. If adding a red node breaks the red-black rules, the fix-up step uses recoloring and rotations to restore balance.
