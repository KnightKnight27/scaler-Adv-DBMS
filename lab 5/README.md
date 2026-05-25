# Lab 5 - B-Tree and Red-Black Tree Implementation

## Objective

Implement and demonstrate the two balanced tree structures used internally by database systems for efficient indexing — the **B-Tree** and the **Red-Black Tree** — in C++.

---

## Files

| File | Description |
|------|-------------|
| `btree.hpp` | B-Tree class declaration (order T=3, node split, insert, traverse, search) |
| `btree.cpp` | Full B-Tree implementation with node splitting and level-print |
| `rbt.hpp`   | Red-Black Tree declaration (sentinel NIL, rotations, fixInsert) |
| `rbt.cpp`   | Full RBT implementation with all 6 rotation/recolour cases + visual print |
| `main.cpp`  | Demo: inserts 13 keys into B-Tree, 12 keys into RBT, runs traversals & searches |
| `CMakeLists.txt` | CMake build config (C++17) |

---

## How to Build & Run

### Using g++ directly
```bash
cd "lab 5"
g++ -std=c++17 -o lab5 main.cpp btree.cpp rbt.cpp
./lab5
```

### Using CMake
```bash
cd "lab 5"
mkdir build && cd build
cmake ..
cmake --build .
./lab5
```

---

## B-Tree (Order T = 3)

### Properties
- Each node holds **at most 2T−1 = 5 keys** and **at most 2T = 6 children**
- Each non-root node holds **at least T−1 = 2 keys**
- All leaf nodes are at the **same depth**
- Insertion uses **proactive splitting** (split full nodes on the way down)

### Key Operations

| Operation | Description |
|-----------|-------------|
| `insert(key)` | Splits root if full, then calls `insertNonFull` recursively |
| `splitChild(i, y)` | Promotes middle key of full child `y` into parent |
| `traverse()` | In-order (sorted) traversal of all keys |
| `search(key)` | Recursive binary search within each node |
| `printTree()` | Level-by-level structural dump |

### Sample Output (keys: 10,20,5,6,12,30,7,17,3,25,40,50,1)

```
=== B-Tree Structure ===
Level 0: [10, 20]
Level 1: [1, 3, 5, 6, 7]
Level 1: [12, 17]
Level 1: [25, 30, 40, 50]
========================
B-Tree In-Order Traversal: 1 3 5 6 7 10 12 17 20 25 30 40 50
```

---

## Red-Black Tree

### Properties (enforced after every insert)
1. Every node is **RED** or **BLACK**
2. The **root** is always **BLACK**
3. Every **NIL leaf** (sentinel) is **BLACK**
4. A **RED** node's children are both **BLACK** (no two consecutive reds)
5. All paths from a node to descendant NILs have the **same black-height**

### Fix-Insert Cases (6 symmetric cases)

| Case | Trigger | Action |
|------|---------|--------|
| 1 | Uncle is RED | Recolour parent, uncle → BLACK; grandparent → RED; move z up |
| 2 | Uncle is BLACK, z is right child | Left-rotate parent; fall to Case 3 |
| 3 | Uncle is BLACK, z is left child | Right-rotate grandparent; swap colours |
| 1′–3′ | Mirror of 1–3 (parent is right child) | Right-rotate / Left-rotate mirror |

### Sample Output (keys: 15,25,8,3,12,20,30,1,5,10,18,28)

```
=== Red-Black Tree Structure ===
R----15(BLACK)
     L----8(RED)
     |    L----3(BLACK)
     |    |    L----1(RED)
     |    |    R----5(RED)
     |    R----12(BLACK)
     |         L----10(RED)
     R----25(RED)
          L----20(BLACK)
          |    L----18(RED)
          R----30(BLACK)
               L----28(RED)
================================
RBT Inorder Traversal: 1(R) 3(B) 5(R) 8(R) 10(R) 12(B) 15(B) 18(R) 20(B) 25(R) 28(R) 30(B)
```

---

## Comparison: B-Tree vs Red-Black Tree

| Feature | B-Tree (T=3) | Red-Black Tree |
|---------|-------------|----------------|
| Node fanout | Up to 2T children | Binary (2 children) |
| Use in DBs | Table/index pages (e.g. SQLite, InnoDB) | In-memory indexes (e.g. `std::map`) |
| Balance mechanism | Node splitting & merging | Rotations & recolouring |
| Height | O(log_T n) — very flat | O(log n) — taller |
| Disk I/O efficiency | Excellent (large pages) | Poor (one key/node) |
| Search complexity | O(log n) | O(log n) |

---

## Learning Outcomes

- B-Tree node splitting maintains sorted order across levels
- Red-Black Tree rotations preserve BST order while restoring RB invariants
- Both structures guarantee **O(log n)** insert and search
- SQLite uses B-Trees for both table storage and secondary indexes (Lab 4)
- `std::map` / `std::set` in C++ STL use Red-Black Trees internally