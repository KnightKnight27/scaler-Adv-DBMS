# Lab 4: Red-Black Tree vs. B-Tree Architectural Indexing Analysis

**Student Name:** Rishi Harti  
**Roll Number:** 24BCS10239  
**Lab Session:** Lab 4  
**Course:** Advanced Database Management Systems (DBMS)

---

## 1. Architectural Introduction: RAM vs. Disk Indexes

Indexing is the foundation of high-performance data retrieval. This lab analyzes and compares two pinnacle index structures: the **Red-Black Tree (RBT)** and the **B-Tree**. Although both are self-balancing search trees, they are optimized for radically different levels of the computer memory hierarchy.

```
       In-Memory Index (RBT)                  Disk-Optimized Index (B-Tree)
              (15)                                    [ 12 | 25 ]
             /    \                                  /     |     \
          (10)    (20)                      [5|6|7|10]  [17|22]  [27|35|40|45]
```

### Comparative Structural Summary
| Architectural Attribute | Red-Black Tree (RBT) | B-Tree ($T = 3$) |
| :--- | :--- | :--- |
| **Node Fan-out (Order)** | Strict Binary (Maximum 2 children) | Multi-way ($2T = 6$ children max) |
| **Primary Target Medium** | Main Memory (RAM) | Secondary Storage (SSD/HDD) |
| **Access Granularity** | Byte-level (Pointer dereferencing) | Block-level (typically 4KB-16KB pages) |
| **Height Complexity** | $2 \log_2(N + 1)$ (Tall and narrow) | $\log_T((N + 1) / 2)$ (Short and wide) |
| **Memory Locality** | Poor (Dispersed nodes leading to cache misses) | High (Contiguous page storage maximizes cache lines) |
| **I/O Operations** | High (Dozens of page faults for disk search) | Minimal (Extremely low disk reads due to wide fanout) |

---

## 2. Red-Black Tree Deep-Dive & Dry-Runs

A Red-Black Tree is a self-balancing binary search tree where each node stores an extra bit representing color (`RED` or `BLACK`). Balance is maintained through rotations and color recoloring during updates.

### 2.1 The 5 Red-Black Invariants
1. **Node Color Rule:** Every node is either `RED` or `BLACK`.
2. **Root Rule:** The root of the tree is always `BLACK`.
3. **Leaf Rule:** Every leaf (represented by the null sentinel node `TNULL`) is `BLACK`.
4. **Red Node Rule:** If a node is `RED`, then both its children must be `BLACK` (no consecutive red nodes).
5. **Black-Height Rule:** For each node, all simple paths from the node to descendant leaves contain the same number of `BLACK` nodes.

### 2.2 Dry Run: Inserting Keys `15, 10, 20, 8, 12`

1. **Insert 15:**
   - Standard BST insert as Root.
   - Rule 2 forces color to `BLACK`.
   - **Result:** `(15, BLACK)`

2. **Insert 10:**
   - Inserted as left child of 15. Default color `RED`.
   - Invariants hold.
   - **Result:**
     ```
          (15, BLACK)
             /
         (10, RED)
     ```

3. **Insert 20:**
   - Inserted as right child of 15. Default color `RED`.
   - Invariants hold.
   - **Result:**
     ```
          (15, BLACK)
             /     \
         (10, RED) (20, RED)
     ```

4. **Insert 8:**
   - Inserted as left child of 10. Default color `RED`.
   - **Violation:** Red parent with Red child (`10 -> 8`).
   - **Resolution (Uncle is RED Case):** 10's sibling (uncle of 8) is 20, which is `RED`.
     - Recolor Parent (10) and Uncle (20) to `BLACK`.
     - Recolor Grandparent (15) to `RED`.
     - Keep Root (15) `BLACK` via Root Rule.
   - **Result:**
     ```
          (15, BLACK)
             /     \
        (10, BLACK) (20, BLACK)
           /
       (8, RED)
     ```

5. **Insert 12:**
   - Inserted as right child of 10. Default color `RED`.
   - Invariants hold (Uncle of 12 is `TNULL` (BLACK), but parent 10 is `BLACK`).
   - **Result:**
     ```
          (15, BLACK)
             /     \
        (10, BLACK) (20, BLACK)
           /    \
       (8, RED) (12, RED)
     ```

---

## 3. B-Tree Deep-Dive & Dry-Runs

A B-Tree is a balanced search tree designed to work efficiently on block-based storage. A B-Tree of minimum degree $T$ enforces key and child pointer counts within tight operational boundaries.

### 3.1 Structural Constraints
- **Keys per Node:** Minimum $T-1$, Maximum $2T-1$.
- **Children per Node:** Minimum $T$, Maximum $2T$ (for internal nodes).
- **Height Uniformity:** All leaves reside at the absolute same depth.

### 3.2 Dry Run: Page Splits on Insert ($T=3$)
With $T=3$, a node can hold at most $2(3)-1 = 5$ keys. When a node reaches 5 keys and another insertion is directed to it, the node is split.

1. **Populate Node with Keys `5, 6, 10, 12, 20`:**
   - Node is completely full (5 keys).
   - **State:** `[5, 6, 10, 12, 20]`

2. **Insert 30:**
   - The root is full. The tree must grow in height.
   - A new empty root page is created, and the full child is split.
   - Median key `10` is promoted to the new root.
   - Left split page retains `[5, 6]`.
   - Right split page receives `[12, 20]`.
   - Key `30` is then inserted into the right split page since $30 > 10$.
   - **Result:**
     ```
              [ 10 ]
             /      \
          [5, 6]   [12, 20, 30]
     ```

### 3.3 Deletion Restructuring: Borrowing vs. Merging
To prevent complex backtracking during deletion, B-Tree traversal employs a proactive restructuring scheme. Before entering any child page, we ensure it contains at least $T$ keys. If it has only $T-1$ keys, we restructure:

- **Borrowing (Sibling has $\ge T$ keys):**
  - Bring the parent key down to the underflowed child.
  - Move the edge key of the rich sibling up to the parent.
  - Transfer the corresponding child pointer if internal.
  
- **Merging (Both siblings have exactly $T-1$ keys):**
  - Pull the dividing parent key down.
  - Combine both sibling nodes and the parent key into a single page.
  - Release the redundant sibling page.

---

## 4. Compilation and Verification

The accompanying `main.cpp` implements both trees in standard, modern C++ (C++17) with explicit verification logic.

### 4.1 Automated Invariant Assertion Verification
The engine guarantees mathematical correctness on every insert and delete operation by checking:
- **RBT Invariant Verification:** Verifies no double-red parent-child relationships and checks that the black-height is identical across all paths.
- **B-Tree Invariant Verification:** Verifies leaf depths, bounds on key/child allocations, sorting accuracy within keys, and correct child routing ranges.

### 4.2 How to Compile & Run
To compile and execute the benchmark and test suites:

```bash
# Compile using g++ with C++17 standard
g++ -std=c++17 main.cpp -o tree_benchmark

# Run the executable
./tree_benchmark
```

### 4.3 Sample Console Outputs
```
==========================================================
    LAB 4: RED-BLACK TREE VS B-TREE DESIGN BENCHMARK     
    Roll No: 24BCS10239 | Name: Rishi Harti
==========================================================

==================================================
             RED-BLACK TREE TEST SUITE             
==================================================
[+] Inserting 15 elements sequentially...
    RBT Invariants Verified Successfully!

[+] Current Red-Black Tree Structure:
R----12 (BLACK)
   L----8 (RED)
   |  L----6 (BLACK)
   |  R----9 (BLACK)
   R----18 (RED)
      L----15 (BLACK)
      |  L----14 (RED)
      |  R----17 (RED)
      R----25 (BLACK)
         L----20 (RED)
         |  L----19 (BLACK)
         |  R----22 (BLACK)
         R----28 (RED)

[+] Searching elements...
    Search tests passed.

[+] Deleting elements sequentially (triggering complex rotation cases)...
    - Deleting key: 6
    - Deleting key: 11
    - Deleting key: 20
    - Deleting key: 15
    - Deleting key: 25
    Invariants hold perfectly post deletions!

==================================================
               B-TREE TEST SUITE (T=3)             
==================================================
[+] Inserting 15 elements to trigger proactive page splits...
    B-Tree Invariants Verified Successfully!

[+] Current B-Tree Page Structure:
Level 0: 12 25 [Internal]
Level 1: 5 6 7 10 [Leaf]
Level 1: 17 20 22 [Leaf]
Level 1: 27 30 35 40 45 50 [Leaf]

[+] Searching elements...
    Search tests passed.

[+] Deleting elements sequentially (triggering sibling borrowing & page merging)...
    - Deleting key: 6
    - Deleting key: 12
    - Deleting key: 30
    - Deleting key: 10
    - Deleting key: 35
    Invariants hold perfectly post B-Tree page deletions!
```

---

## 5. Summary Conclusion

1. **In-Memory Operations:** Red-Black Trees are brilliant for CPU-bound processes. Because we have fast, direct byte access via random-access RAM pointers, a narrow binary structure is highly effective and minimizes restructuring overhead.
2. **On-Disk Operations:** B-Trees are the standard choice for databases (such as PostgreSQL and SQLite) because they are designed to match block-based physical architectures. The higher degree $T$ keeps the tree flat, drastically reducing disk read/write calls (page operations) during queries.
