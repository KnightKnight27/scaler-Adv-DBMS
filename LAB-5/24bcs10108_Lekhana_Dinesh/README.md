# Lab 5 - Red-Black Tree Implementation

Student name: Lekhana Dinesh  
Roll number: 24BCS10108

## Objective

The objective of this lab is to implement a Red-Black Tree in C++ and demonstrate insertion, traversal, searching, and validation.

## Red-Black Tree properties

A Red-Black Tree follows these rules:

- Every node is either red or black.
- The root node is always black.
- All NIL leaf nodes are black.
- A red node cannot have a red child.
- Every path from a node to its NIL descendants has the same number of black nodes.
- The tree also follows Binary Search Tree ordering.

## Implementation details

- `Node` stores key, color, left, right, and parent pointers.
- A sentinel `NIL` node is used instead of normal `nullptr` leaves.
- `insert()` inserts the value like a normal Binary Search Tree.
- `insertFixup()` fixes Red-Black Tree violations using recoloring and rotations.
- `leftRotate()` and `rightRotate()` are used to balance the tree.
- The program includes search, inorder traversal, level-order traversal, and validation.
- Validation checks root color, red node rules, BST ordering, and black height consistency.

## Compile and run commands

Compile:

```bash
g++ -std=c++17 red_black_tree.cpp -o red_black_tree.exe
```

Run:

```bash
./red_black_tree.exe
```

On Windows, if there is a MinGW DLL issue, use static compilation:

```bash
g++ -std=c++17 -static -static-libgcc -static-libstdc++ red_black_tree.cpp -o red_black_tree.exe
```

## Verified output

```text
Red-Black Tree validation: VALID
Inorder traversal: 1(B) 2(R) 3(B) 7(R) 8(B) 11(R) 12(B) 18(R) 19(B) 20(R) 21(B) 22(R) 29(R) 31(B) 38(B) 41(B)
Level-order traversal: 19(B) 8(B) 38(B) 2(R) 12(B) 29(R) 41(B) 1(B) 3(B) 11(R) 18(R) 21(B) 31(B) 7(R) 20(R) 22(R)
Search 19: FOUND
Search 100: NOT FOUND
```

## Conclusion

The Red-Black Tree stays balanced by using recoloring and rotations after insertion. This keeps operations like search and insertion efficient while maintaining the required Red-Black Tree properties.