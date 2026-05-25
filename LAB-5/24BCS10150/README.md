# Lab 5 - Red Black Tree Implementation

## Objective

The objective of this lab is to understand and implement the Red-Black Tree data structure and perform operations such as insertion, traversal, and searching while maintaining tree balancing properties.

---

## Files Included

- `red_black_tree.cpp` → C++ implementation of Red-Black Tree
- `README.md` → Documentation and explanation of implementation

---

## Features Implemented

- Node insertion
- Left rotation
- Right rotation
- Balancing using Red-Black properties
- Inorder traversal
- Element searching

---

## Red-Black Tree Properties

1. Every node is either RED or BLACK.
2. Root node is always BLACK.
3. Red nodes cannot have red children.
4. Every path from root to leaf contains the same number of black nodes.
5. NULL nodes are considered BLACK.

---

## Compilation Command

```bash
g++ red_black_tree.cpp -o rbt
```

---

## Execution Command

```bash
./rbt
```

---

## Sample Output

```text
Inorder Traversal of Red-Black Tree:
5 (RED) 10 (BLACK) 15 (RED) 20 (BLACK) 25 (RED) 30 (BLACK)

Element 15 found in tree.
Element 100 not found in tree.
```

---

## Observations

- Tree remains balanced after insertions.
- Rotations and recoloring help maintain Red-Black properties.
- Search operation works efficiently with logarithmic complexity.

---

## Conclusion

This lab helped in understanding self-balancing binary search trees and how Red-Black Trees maintain efficient operations through rotations and color balancing techniques.