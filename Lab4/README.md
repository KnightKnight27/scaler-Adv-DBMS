# Lab Session 4: Red-Black Tree & Full B-Tree in C++

## Objective
For this lab assignment, I implemented a Red-Black Tree (a self-balancing BST often used in database index structures) and a full B-Tree (the actual on-disk index structure used by engines like PostgreSQL, MySQL, and SQLite). The implementations support inserting, merging (split promotion), and deleting with underflow merging.

---

## Part 1: Red-Black Tree

### Properties
I ensured the following properties were maintained in my implementation to guarantee O(log n) height:
1. Every node is colored either Red or Black.
2. The root node is always Black.
3. No two consecutive Red nodes can exist (a Red node's parent must be Black).
4. Every path from a node to its NULL descendants contains the same number of Black nodes.

### Implementation
I wrote the `RedBlackTree` class in `rbt.cpp`. It includes the necessary rotations (`left_rotate`, `right_rotate`) and fixup operations (`fix_insert`, `fix_delete`) to keep the tree balanced after modifications. 

To compile and run my code, use:
```bash
g++ -std=c++17 -o rbt rbt.cpp && ./rbt
```

---

## Part 2: Full B-Tree (order t)

For the B-Tree implementation, I configured the order `T = 2` as a default. In this structure, every internal node holds between `t-1` and `2t-1` keys and has between `t` and `2t` children. Leaves hold `t-1` to `2t-1` keys. The root node may hold as few as 1 key.

### Supported operations:
- **Insert**: Handles splitting full nodes on the way down.
- **Search**: Recursively finds the correct key.
- **Delete**: Merges or borrows keys from siblings on the way down to handle underflow.

The B-Tree logic I created is contained in `btree.cpp`. 

To compile and run this portion, use:
```bash
g++ -std=c++17 -o btree btree.cpp && ./btree
```

---

## Red-Black Tree vs B-Tree — When to use which

During my analysis, I noted several differences between the two data structures:

| Property           | Red-Black Tree                        | B-Tree (order t)                          |
|--------------------|---------------------------------------|-------------------------------------------|
| Storage            | In-memory                             | Designed for disk (large node = 1 page)   |
| Node size          | 1 key per node                        | Up to `2t-1` keys per node                |
| Height             | O(log n)                              | O(log_t n) — much shorter for large t     |
| Use in databases   | In-memory indexes, std::map           | On-disk indexes (PostgreSQL, MySQL, InnoDB)|
| Cache friendliness | Poor (pointer chasing)                | Excellent (sequential keys in one node)   |
| Merge/split        | Rotation only                         | Child split / sibling borrow / merge      |

PostgreSQL's B-Tree index pages are 8 KB by default, which perfectly matches the `page_size` we explored in Lab 2. This means one disk read fetches an entire B-Tree node, making it highly efficient.

---

## Key Takeaways
- I learned that Red-Black Trees maintain balance through coloring and rotation, making them ideal for in-memory sorted maps.
- B-Trees minimize disk I/O by packing many keys into one node, which equals one page read.
- Implementing deletion in B-Trees involves three distinct cases: borrowing from a sibling, merging with a sibling, or replacing the target with its predecessor/successor.
- The minimum degree `T` directly controls the fanout and tree height. A higher `T` results in a shorter tree, which in turn means fewer disk seeks.
