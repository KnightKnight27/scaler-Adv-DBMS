# Lab 5 — Self-Balancing BST: Red-Black Tree from Scratch

> **Course:** Advanced Database Management Systems  
> **Author:** Vishudh Goel (24bcs10162)  
> **Language:** C++17  

A ground-up implementation of a **Red-Black Tree (RBT)** — a balanced binary search tree that assigns a colour attribute (red or black) to every node and enforces a compact set of structural invariants to maintain `O(log n)` worst-case height. Red-Black Trees are the primary data structure powering ordered index lookups in real-world database engines (e.g. PostgreSQL's internal `RBTree`, the Linux kernel scheduler, and the C++ Standard Library's `std::map` / `std::set`).

---

## Contents
1. [Lab Deliverables](#lab-deliverables)
2. [Compilation & Execution](#compilation--execution)
3. [Demo Output](#demo-output)
4. [Exposed Interface](#exposed-interface)
5. [Red-Black Invariants](#red-black-invariants)
6. [Rebalancing After Insertion](#rebalancing-after-insertion)
7. [Rebalancing After Deletion](#rebalancing-after-deletion)
8. [Rotation Mechanics](#rotation-mechanics)
9. [The Sentinel Node Approach](#the-sentinel-node-approach)
10. [Asymptotic Costs](#asymptotic-costs)

---

## Lab Deliverables

| File | Role |
| :--- | :--- |
| `RedBlackTree.h` | Header file — `TreeNode` struct, `Colour` enum, public + private method declarations. |
| `RedBlackTree.cc` | Complete implementation: insertion, search, deletion, rotations, fixup routines, BFS visualiser. |
| `main.cc` | Driver program that demonstrates insert / search / remove with a representative dataset. |
| `Makefile` | Build script: compiles `rbtree` with `g++ -std=c++17 -Wall -Wextra -O2`. |
| `README.md` | This analysis document. |

---

## Compilation & Execution

```bash
make          # compiles → ./rbtree
make run      # compiles and executes
make clean    # removes the binary
```

To compile manually without Make:
```bash
g++ -std=c++17 -Wall -Wextra -O2 -o rbtree main.cc RedBlackTree.cc
./rbtree
```

To activate detailed scenario-dispatch logging, uncomment `#define VERBOSE_LOG` in `RedBlackTree.cc` and recompile.

---

## Demo Output

```
Inserting keys: 12 25 36 18 28 7 3 9 45 33
```
The output uses **LeetCode-style BFS level-order** notation. Every node is tagged with its colour (`R` = red, `B` = black), and `null` marks an absent child slot. Visualising the first tree:

```
                  25B
                /     \
             12R       36R
            /   \     /   \
          7B    18B  28B   45B
         /  \              /
        3R   9R          33R
```

---

## Exposed Interface

```cpp
class RBTree {
public:
    RBTree();
    ~RBTree();                        // deallocates every node + SENTINEL

    bool search(int key);             // O(log n) key lookup
    void insert(int key);             // O(log n) insertion; duplicates placed left
    void remove(int key);             // O(log n) deletion; no-op if key absent

    void printLevelOrder();           // BFS dump of the tree
};
```

---

## Red-Black Invariants

Every valid Red-Black Tree must satisfy these **five constraints** simultaneously:

1. Each node is coloured either **RED** or **BLACK**.
2. The **root** node is always BLACK.
3. All **SENTINEL leaf nodes** are BLACK.
4. **No two consecutive RED nodes** may appear on any path (a RED node cannot have a RED child).
5. Every path from a given node to any of its descendant SENTINEL leaves passes through the **same number of BLACK nodes** (the *black-height* property).

Constraints 4 and 5 together bound the tree height at `2 · log₂(n + 1)`, guaranteeing logarithmic operations.

---

## Rebalancing After Insertion

Freshly inserted nodes always enter as **RED**. This preserves invariant 5 (black-height unchanged) but may violate invariant 4 (red parent ← red child). The `rebalanceAfterInsert(z)` routine walks upward, dispatching to one of four scenarios:

### Scenario 0 — Parent is BLACK (or `z` is the root)
No violation exists. The fixup terminates.

### Scenario 1 — Parent RED, Uncle RED
Flip parent and uncle to **BLACK**, grandparent to **RED**. The local subtree is now valid, but the grandparent may have created a new red-red clash further up. Recurse on the grandparent.

```
    GP(B)              GP(R)        ← recurse here
    /  \      ==>      /  \
  P(R)  U(R)         P(B)  U(B)
   \                   \
    z(R)                z(R)
```

### Scenario 2 — Parent RED, Uncle BLACK, `z` is the inner grandchild (LR or RL)
The path from grandparent through parent to `z` forms a "zigzag". A rotation around the parent converts this into the straight-line configuration of Scenario 3.

```
    GP(B)               GP(B)
    /  \                /  \
  P(R)  U(B)   ==>    z(R)  U(B)       (leftRotate around P)
    \                 /
     z(R)           P(R)
```

### Scenario 3 — Parent RED, Uncle BLACK, `z` is the outer grandchild (LL or RR)
Recolour parent to **BLACK**, grandparent to **RED**, then rotate around the grandparent. This resolves the violation and preserves the subtree's black-height.

```
       GP(B)              P(B)
       /  \               /  \
     P(R)  U(B)  ==>    z(R)  GP(R)
     /                          \
    z(R)                        U(B)
```

As a safety measure, the root is explicitly painted BLACK after every insertion cycle.

---

## Rebalancing After Deletion

Deletion follows the canonical **CLRS** Red-Black Delete procedure:

1. **BST-Delete** the target node `z`:
   - If `z` has at most one real child, splice it out using `replaceSubtree()`.
   - If `z` has two children, locate its **in-order successor** `y` (leftmost node of the right subtree), transplant `y` into `z`'s position, and inherit `z`'s colour.
2. Let `y` represent the node physically removed/moved, and `x` represent its replacement.
3. If `y`'s **original colour was BLACK**, the removal creates a "double-black" deficit on paths through `x`. Invoke `rebalanceAfterDelete(x)` to repair it.

The `rebalanceAfterDelete(x)` routine examines four mirrored sibling-based cases:

| Case | Sibling `w` Configuration | Resolution |
| :---: | :--- | :--- |
| **A** | Sibling is RED | Recolour sibling BLACK, parent RED, rotate. Reduces to case B/C/D. |
| **B** | Sibling BLACK, both children BLACK | Paint sibling RED; propagate the deficit to parent. |
| **C** | Sibling BLACK, near child RED, far child BLACK | Rotate around sibling to make far child RED → becomes case D. |
| **D** | Sibling BLACK, far child RED | Recolour + rotate around parent. **Terminal fix** — loop ends. |

The loop exits upon reaching the root or completing case D. Finally, `x` is painted BLACK to clear any residual double-black.

---

## Rotation Mechanics

Both insertion and deletion fixups use two fundamental rotations:

### `leftRotate(x)`
```
       x                   y
      / \                 / \
     a   y      ==>      x   c
        / \             / \
       b   c           a   b
```

### `rightRotate(x)`
```
        x                  y
       / \                / \
      y   c     ==>      a   x
     / \                    / \
    a   b                  b   c
```

Each rotation executes in `O(1)` time, rewiring exactly three parent-child pointer pairs while preserving the BST ordering invariant.

---

## The Sentinel Node Approach

Rather than using raw `nullptr` for empty leaves, this implementation allocates a single shared **SENTINEL** node (coloured BLACK) that represents all absent children. This design brings several advantages:

- During deletion fixup, `x->parent` can safely be dereferenced even when `x` is a leaf — `replaceSubtree()` explicitly sets `SENTINEL->parent` for this reason.
- Colour checks against empty leaves require no special-casing since `SENTINEL->colour` is always `BLACK`.
- An entire class of null-pointer dereference bugs is eliminated.

The SENTINEL is heap-allocated in the constructor and freed alongside all tree nodes in the destructor (via recursive post-order traversal in `deallocate()`).

---

## Asymptotic Costs

| Operation | Time Complexity | Space Overhead |
| :--- | :---: | :---: |
| `search` | `O(log n)` | `O(1)` |
| `insert` | `O(log n)` amortised — at most `O(log n)` recolours + `O(1)` rotations | `O(1)` |
| `remove` | `O(log n)` — at most `O(log n)` recolours + `O(1)` rotations (≤ 3) | `O(1)` |
| `printLevelOrder` | `O(n)` | `O(n)` |

The strict height bound of `2 · log₂(n + 1)` is what makes RBTs the preferred balanced-tree variant for index structures in production databases.

---
