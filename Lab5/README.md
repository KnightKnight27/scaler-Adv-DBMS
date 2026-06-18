# Red-Black Tree Implementation

## Objective

To implement a Red-Black Tree in C++ and understand self-balancing binary search tree operations.

## Language Used

C++

## Features Implemented

- Node insertion
- Left rotation
- Right rotation
- Rebalancing after insertion
- Inorder traversal

## Compilation

```bash
g++ red_black_tree.cpp -o rbt
```

## Execution

```bash
./rbt
```

## Sample Output

```text
Red-Black Tree Inorder Traversal:
5(R) 10(B) 15(R) 20(B) 25(R) 30(B)
```

## Red-Black Tree Properties

- Every node is either red or black
- Root is always black
- Red nodes cannot have red children
- Every path has same black height
- Tree remains approximately balanced