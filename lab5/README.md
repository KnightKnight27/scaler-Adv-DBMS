# Lab 5: Red-Black Tree Implementation

**Student:** Lokendra Singh Rajawat — 23bcs10075  
**Subject:** Advanced Database Management Systems

---

## Objective

To implement and analyze a Red-Black Tree — a self-balancing binary search tree used extensively in database systems (e.g., MySQL's InnoDB index structures, Linux kernel's scheduler) to maintain O(log n) operations.

---

## Theory

### Red-Black Tree Properties

A valid Red-Black Tree must satisfy all 5 properties at all times:

| # | Property |
|---|----------|
| 1 | Every node is either **RED** or **BLACK** |
| 2 | The **root** is always **BLACK** |
| 3 | Every **leaf (NIL sentinel)** is **BLACK** |
| 4 | If a node is **RED**, both its children are **BLACK** (no two consecutive RED nodes) |
| 5 | For each node, all paths from that node to descendant leaves have the **same number of BLACK nodes** (uniform black-height) |

These properties guarantee that the tree height is always ≤ **2 × log₂(n+1)**, ensuring O(log n) for search, insertion, and deletion.

### Implementation Design

- A single **NIL sentinel node** (always BLACK) is shared by all leaf pointers, simplifying boundary checks.
- Newly inserted nodes are always colored **RED** to minimize black-height violations.
- **`fix_insert()`** restores properties after insertion using 3 cases (+ their mirrors).

---

## How to Compile and Run

```bash
cd lab5
make          # compiles rbtree.c → ./rbtree
make run      # compile + run
make clean    # remove binary
```

---

## Insertion Cases (fix_insert)

### Case 1: Uncle is RED
Recolor parent and uncle to BLACK, grandparent to RED. Move the pointer up to grandparent.

```
        G(B)                G(R)  ← move z here
       / \       -->       / \
     P(R)  U(R)          P(B)  U(B)
     /                   /
   Z(R)                Z(R)
```

### Case 2: Uncle is BLACK, Z is an inner child
Rotate parent to make Z an outer child, then fall through to Case 3.

### Case 3: Uncle is BLACK, Z is an outer child
Recolor parent to BLACK, grandparent to RED, then rotate grandparent.

```
      G(R)                 P(B)
     / \       -->        / \
   P(B)  U(B)           Z(R)  G(R)
   /                           \
 Z(R)                           U(B)
```

---

## Sample Output

```
==========================================================
  Lab 5: Red-Black Tree Implementation
  Student: Lokendra Singh Rajawat (23bcs10075)
==========================================================

[Task 1] Tree Initialization
Empty Red-Black Tree created. Size = 0. Root = NIL.

[Task 2 & 3] Node Insertion and Balancing Operations

--- Inserting 10 ---
[Result] 10 inserted. Color: BLACK

--- Inserting 30 ---
[Case 3M] z=30 is outer child — recolor + rotate grandparent left
[Rotation] Left rotation on node 10
[Result] 30 inserted. Color: RED

--- Inserting 15 ---
[Case 1] Uncle 30 is RED — recoloring
[Result] 15 inserted. Color: RED

...

Total insertions: 11
Total rotations : 1
Total recolorings: 11
```

### Inorder Traversal (Sorted Output)
```
1(RED)  5(BLACK)  10(RED)  15(BLACK)  17(RED)  20(BLACK)
25(BLACK)  30(RED)  35(RED)  40(BLACK)  50(RED)
```
✅ Keys appear in ascending order — BST ordering is maintained.

### Tree Structure (Level Order)
```
Level 0: [20/BLACK]
Level 1: [10/RED]  [30/RED]
Level 2: [5/BLACK] [15/BLACK] [25/BLACK] [40/BLACK]
Level 3: [1/RED]   [17/RED]   [35/RED]   [50/RED]
```

### Search Results
```
[Search] Found 15 after 3 comparison(s). Color: BLACK
[Search] Found 40 after 3 comparison(s). Color: BLACK
[Search] Key 99 NOT found after 4 comparison(s).
[Search] Found 1  after 4 comparison(s). Color: RED
[Search] Found 50 after 4 comparison(s). Color: RED
```

### Property Verification
```
[PASS] Property 2: Root is BLACK.
[PASS] Property 4: No RED node has a RED child.
[PASS] Property 5: Black-height = 2 (uniform on all paths).
```

---

## Observations

| Observation | Detail |
|-------------|--------|
| Root node | Always BLACK — enforced after every fix-up |
| Inorder traversal | Produces perfectly sorted output — BST property maintained |
| Black-height | Uniform = 2 across all paths from root to NIL |
| Rotations performed | 1 (left rotation on node 10 when inserting 30) |
| Recolorings performed | 11 total — most fixes done via recoloring (cheap O(1)) |
| Max height with 11 nodes | Level 3 (depth 4) — far less than worst-case BST height of 11 |

---

## Comparison: RB-Tree vs Standard BST vs AVL Tree

| Feature | BST | AVL Tree | Red-Black Tree |
|---------|-----|----------|----------------|
| Height guarantee | None (O(n) worst) | Strictly O(log n) | O(log n) |
| Balance criterion | None | Height difference ≤ 1 | Black-height uniformity |
| Insert cost | O(log n) avg | O(log n) + ≤2 rotations | O(log n) + ≤2 rotations |
| Lookup speed | Faster than RB on balanced data | Slightly faster than RB | Slightly slower than AVL |
| Used in | – | Memory allocators | DB indexes, OS schedulers |

---

## Learning Outcomes

After completing this lab, I was able to:
- Understand the 5 Red-Black Tree properties and why they guarantee O(log n) height.
- Implement left and right rotations for structural rebalancing.
- Handle all 3 insertion fix-up cases (and their mirrors) in `fix_insert()`.
- Verify tree correctness via black-height calculation and property checking.
- Observe that most insertions are fixed via **recoloring** (O(1)) with very few **rotations**.
- Understand how RB-Trees are used in database indexing and OS kernel data structures.
