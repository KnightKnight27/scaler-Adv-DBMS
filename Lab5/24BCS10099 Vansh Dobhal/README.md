# Lab 5 - B-Tree and Red-Black Tree Implementation

**Name:** Vansh Dobhal  
**Roll Number:** 24BCS10099  
**Course:** Advanced DBMS - Scaler School of Technology

## Objective

This lab implements two balanced tree structures in C++:

- B-Tree with minimum degree `3`
- Red-Black Tree with insert balancing through recoloring and rotations

Both structures are important in database systems. B-Trees are commonly used for disk/page-oriented indexes, while Red-Black Trees are often used for in-memory ordered maps and sets.

---

## Files

| File | Description |
|------|-------------|
| `btree.hpp` | B-Tree class and node declarations. |
| `btree.cpp` | B-Tree insertion, splitting, traversal, search, and level printing. |
| `rbt.hpp` | Red-Black Tree node, color enum, and class declaration. |
| `rbt.cpp` | Red-Black Tree insertion, rotations, fix-up logic, traversal, and printing. |
| `main.cpp` | Demonstrates both trees using separate key sets and search tests. |
| `CMakeLists.txt` | CMake build configuration using C++17. |

---

## Build and Run

Using `g++` directly:

```bash
cd "Lab5/24BCS10099 Vansh Dobhal"
g++ -std=c++17 -Wall -Wextra -pedantic -o lab5 main.cpp btree.cpp rbt.cpp
./lab5
```

Using CMake:

```bash
cd "Lab5/24BCS10099 Vansh Dobhal"
mkdir -p build
cd build
cmake ..
cmake --build .
./lab5
```

---

## B-Tree Implementation

The B-Tree uses `BTREE_MIN_DEGREE = 3`.

### Properties

- A node can contain at most `2t - 1 = 5` keys.
- A node can contain at most `2t = 6` children.
- Every non-root node must contain at least `t - 1 = 2` keys.
- Keys inside each node are stored in sorted order.
- All leaves remain at the same depth.
- When a child is full during insertion, it is split before the algorithm descends into it.

### Implemented Operations

| Operation | Function | Description |
|-----------|----------|-------------|
| Insert | `insert` | Adds a key while preserving B-Tree balance. |
| Split | `splitChild` | Splits a full child and promotes the middle key. |
| Traverse | `traverse` | Prints all keys in sorted order. |
| Search | `search` | Looks for a key recursively. |
| Structure print | `printStructure` | Prints the tree level by level. |

### Demo Keys

```text
42, 12, 7, 29, 18, 55, 3, 9, 33, 47, 61, 1, 24, 38, 50
```

### Observed B-Tree Output

```text
B-Tree structure (minimum degree 3)
Level 0: [18, 42]
Level 1: [1, 3, 7, 9, 12] [24, 29, 33, 38] [47, 50, 55, 61]
B-Tree inorder: 1 3 7 9 12 18 24 29 33 38 42 47 50 55 61
Search 33 in B-Tree: FOUND
Search 99 in B-Tree: NOT FOUND
```

---

## Red-Black Tree Implementation

The Red-Black Tree is implemented as a binary search tree with an explicit black `nil` sentinel node.

### Properties

- Each node is either red or black.
- The root is always black.
- The sentinel `nil` leaves are black.
- A red node cannot have a red child.
- Every path from a node to a descendant `nil` leaf has the same number of black nodes.
- Insertions first behave like normal BST insertions, then repair Red-Black properties.

### Implemented Operations

| Operation | Function | Description |
|-----------|----------|-------------|
| Insert | `insert` | Inserts a key and calls the repair logic. |
| Left rotation | `rotateLeft` | Moves a right child up while preserving BST order. |
| Right rotation | `rotateRight` | Moves a left child up while preserving BST order. |
| Fix insertion | `repairAfterInsert` | Handles uncle-red and rotation cases. |
| Traverse | `inorder` | Prints keys in sorted order with color labels. |
| Search | `search` | Finds a key or returns `nullptr`. |
| Structure print | `printStructure` | Displays the final tree with colors. |

### Demo Keys

```text
36, 18, 52, 9, 27, 45, 60, 4, 14, 22, 31, 40, 48, 57, 63
```

### Observed Red-Black Tree Output

```text
Red-Black Tree structure
R-- 36(BLACK)
    L-- 18(RED)
    |   L-- 9(BLACK)
    |   |   L-- 4(RED)
    |   |   R-- 14(RED)
    |   R-- 27(BLACK)
    |       L-- 22(RED)
    |       R-- 31(RED)
    R-- 52(RED)
        L-- 45(BLACK)
        |   L-- 40(RED)
        |   R-- 48(RED)
        R-- 60(BLACK)
            L-- 57(RED)
            R-- 63(RED)

Red-Black Tree inorder: 4(R) 9(B) 14(R) 18(R) 22(R) 27(B) 31(R) 36(B) 40(R) 45(B) 48(R) 52(R) 57(R) 60(B) 63(R)
Search 22 in Red-Black Tree: FOUND
Search 100 in Red-Black Tree: NOT FOUND
```

---

## Comparison

| Feature | B-Tree | Red-Black Tree |
|---------|--------|----------------|
| Node shape | Multi-key, multi-child node | One key per node, two children |
| Balance method | Split full nodes | Recolor and rotate |
| Typical database use | Disk-backed table and index pages | In-memory ordered structures |
| Height behavior | Very small because fanout is high | Logarithmic but taller than B-Tree |
| Search complexity | `O(log n)` | `O(log n)` |
| Insert complexity | `O(log n)` | `O(log n)` |

---

## Learning Outcome

This lab connects directly to database indexing. The B-Tree demo shows why databases store many keys per page: fewer levels means fewer page reads. The Red-Black Tree demo shows how an in-memory tree can stay balanced using local rotations and recoloring after insertion.