# Lab 5 - Red-Black Tree

## Student Info
- **ID:** 24BCS10031
- **Name:** Prabal Patra

---

## Overview

Implemented Red-Black Tree in C++ with template-based generic design.

---

## Red-Black Tree Properties

1. Every node is either RED or BLACK
2. Root is always BLACK
3. Every leaf (NIL) is BLACK
4. RED node cannot have RED children (no two adjacent RED nodes)
5. Every path from root to leaf has same BLACK height

### Time Complexity

| Operation | Average | Worst |
|-----------|---------|-------|
| Insert | O(log n) | O(log n) |
| Delete | O(log n) | O(log n) |
| Search | O(log n) | O(log n) |

---

## Implementation

### Features

- Template-based (works with any comparable type)
- Insert with fixup (rotations + recoloring)
- Delete with fixup
- Search
- Inorder traversal
- Level order (BFS) traversal
- Thread-safe insert/delete

### Methods

| Method | Description |
|--------|-------------|
| `insert(T key)` | Insert key, fix violations |
| `remove(T key)` | Remove key, fix violations |
| `contains(T key)` | Search for key |
| `size()` | Return node count |
| `inorderTraversal()` | Left-root-right |
| `levelOrderTraversal()` | BFS by levels |

---

## Build & Run

```bash
g++ -std=c++17 rbtree.cpp -o rbtree
./rbtree
```

---

## Test Output

```
Inserting: 7, 3, 18, 10, 22, 8, 11, 26, 2, 14

Inorder:   2(R) 3(B) 7(R) 8(B) 10(B) 11(B) 14(R) 18(R) 22(B) 26(R)

Level Order:
Level 0: 10(B)       <- root
Level 1: 7(R) 18(R)
Level 2: 3(B) 8(B) 11(B) 22(B)
Level 3: 2(R) 14(R) 26(R)

Contains 10: yes
Contains 15: no
```

---

## Why B-trees?

Red-Black trees prepare for B-tree implementation:
- Self-balancing via rotations
- Color-based invariants
- O(log n) operations
- Same principles scale to multi-way branching in B-trees