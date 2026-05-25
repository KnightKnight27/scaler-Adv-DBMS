# Red-Black Tree in C++

A simple implementation of a **Red-Black Tree** in C++ with insertion, balancing using rotations, recoloring, and inorder traversal visualization.

---

## Features

- Self-balancing Binary Search Tree
- Supports:
  - Insertion
  - Left Rotation
  - Right Rotation
  - Recoloring
  - Inorder Traversal
- Maintains Red-Black Tree properties automatically
- Written in clean modern C++

---

## Red-Black Tree Properties

A Red-Black Tree follows these rules:

1. Every node is either **RED** or **BLACK**
2. Root node is always **BLACK**
3. Red nodes cannot have red children
4. Every path from a node to `NULL` nodes contains the same number of black nodes
5. `NULL` leaves are considered BLACK

These properties ensure the tree remains approximately balanced.

---

## Project Structure

```text
.
├── main.cpp
└── README.md
```

---

## How It Works

### Insertion

New nodes are inserted as:

```text
RED
```

After insertion, the tree may violate Red-Black properties.

The algorithm fixes violations using:

- Recoloring
- Left Rotation
- Right Rotation

---

## Rotation Cases

### Left Rotation

Used when the tree becomes right-heavy.

Before:

```text
    x
     \
      y
```

After:

```text
      y
     /
    x
```

---

### Right Rotation

Used when the tree becomes left-heavy.

Before:

```text
      y
     /
    x
```

After:

```text
    x
     \
      y
```

---

## Violation Fixing Cases

### Case 1 — Uncle is RED

- Recolor parent and uncle to BLACK
- Recolor grandparent to RED
- Move upward

---

### Case 2 — Triangle Formation

Examples:
- Left-Right (LR)
- Right-Left (RL)

Convert into Case 3 using one rotation.

---

### Case 3 — Straight Line Formation

Examples:
- Left-Left (LL)
- Right-Right (RR)

Perform rotation on grandparent and recolor.

---

## Example

### Input

```cpp
tree.insert(10);
tree.insert(20);
tree.insert(30);
tree.insert(15);
tree.insert(5);
tree.insert(1);
```

### Output

```text
1(R) 5(B) 10(R) 15(B) 20(B) 30(B)
```

- `(R)` = Red Node
- `(B)` = Black Node

---

## Compilation

Using g++:

```bash
g++ main.cpp -o rbtree
```

Run:

```bash
./rbtree
```

For Windows:

```bash
rbtree.exe
```

---

## Time Complexity

| Operation | Complexity |
|-----------|------------|
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Delete    | O(log n)   |

---

## Concepts Used

- Binary Search Trees
- Self-balancing Trees
- Rotations
- Pointer Manipulation
- Recursion
- Tree Traversal

---

## Future Improvements

- Deletion in Red-Black Tree
- Search operation
- Level-order visualization
- Template-based generic implementation
- Smart pointer support
- Graphical visualization

---

## Author

Built as part of Data Structures & Algorithms practice in C++.
