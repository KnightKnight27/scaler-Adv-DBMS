# Lab 4 - Part 1: Red-Black Tree Implementation in C++

## Objective

Implement a Red-Black Tree supporting insertion, deletion, rotations, and recoloring while maintaining balanced tree properties.

## Features Implemented

- Insert Operation
- Delete Operation
- Left Rotation
- Right Rotation
- Recoloring
- Inorder Traversal

## Red-Black Tree Properties

1. Every node is either Red or Black.
2. The root node is always Black.
3. All NULL leaves are considered Black.
4. A Red node cannot have a Red child.
5. Every path from root to leaf contains the same number of Black nodes.

These properties guarantee a height of O(log n).

## Algorithms Used

### Insertion
- Insert node as in a Binary Search Tree.
- Color new node Red.
- Fix violations using:
  - Recoloring
  - Left Rotation
  - Right Rotation

### Deletion
- Delete node as in BST.
- Restore Red-Black properties using delete-fixup operations.
- Apply rotations and recoloring when required.

## Compilation

```bash
g++ -std=c++17 rbt.cpp -o rbt
```

## Execution

```bash
./rbt
```

## Sample Output

```
Inorder Traversal (Key + Color):
1R 5B 10R 15B 20B 25R 30B

After Deleting 20:
1R 5B 10R 15B 25B 30B
```

## Observations

- Tree remains balanced after insertions.
- Deletion preserves Red-Black properties.
- Search, Insert, and Delete operations execute in O(log n).

## Applications

- STL map and set implementations
- In-memory database indexing
- Operating systems and schedulers
- Associative containers

## Conclusion

The Red-Black Tree maintains balance using rotations and recoloring, ensuring efficient insertion, deletion, and search operations.