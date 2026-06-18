# Lab 5 — Red Black Tree Implementation (C++)

**Name:** Akarsh Garg  
**Roll No:** 24BCS10181  
**Batch:** B  

This project demonstrates a Red-Black Tree implementation in C++ using a balanced Binary Search Tree approach.  
The tree supports insertion, searching, inorder traversal, sideways visual display, and validation of Red-Black Tree properties.

## Files Included

| File Name | Description |
|---|---|
| `RB_tree.cpp` | Contains Red Black Tree implementation and demo execution |

## About Red-Black Tree

A Red-Black Tree is a self-balancing Binary Search Tree where each node stores an additional colour value (**Red** or **Black**).  
The following rules ensure balance in the tree:

1. Root node must always remain black.
2. A red node cannot have another red child.
3. Every path from root to NULL contains the same number of black nodes.
4. Inorder traversal always follows sorted BST order.

Due to these rules, searching and insertion operations run efficiently in **O(log n)** time.

## Insertion Logic

When a new node is inserted:

1. Normal BST insertion is performed.
2. New node colour is initially marked red.
3. Violations are fixed using rotations and recolouring.

Cases handled:

- Parent and uncle both red → recolour nodes.
- Zig-zag structure → rotate parent.
- Straight line imbalance → rotate grandparent and recolour.

## Compilation Command

```bash
cd Lab5/24BCS10181-akarsh-garg
g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 RB_tree.cpp -o rb_tree
```

## Run Program

```bash
./rb_tree
```

## Example Output

```text
Adding values: 15 28 44 19 35 8 4 11 52 47

Tree Structure:
        52(B)
            47(R)
    44(R)
        35(B)
28(B)
        19(B)
    15(R)
            11(R)
        8(B)
            4(R)

Sorted Traversal:
4 8 11 15 19 28 35 44 47 52

Search Results:
find(19) -> present
find(100) -> absent
find(4) -> present

Tree Properties Valid: yes
```
