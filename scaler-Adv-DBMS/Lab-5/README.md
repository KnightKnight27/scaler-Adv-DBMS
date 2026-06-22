# Lab 5: Red-Black Self-Balancing Binary Search Tree

> **Subject:** Advanced Database Management Systems (ADBMS)  
> **Student Name:** Nandani Kumari  
> **Roll Number:** 24bcs10317  
> **Programming Language:** C++17  

---

## 1. Overview & Objectives

This project implements a fully functional **Red-Black BST** (an advanced self-balancing binary search tree) from scratch. Each tree node is augmented with a color bit (Red or Black). By checking and correcting structural violations on modification (insertions/deletions), the tree ensures its depth remains logarithmic: $O(\log n)$ height in the worst case. 

Red-Black Trees are highly valued in database systems for implementing indexing structures, standard collection classes (e.g., `std::set` or `std::map` in C++), and memory allocation subsystems due to their robust execution bounds.

---

## 2. Directory Layout & Artifacts

| Component | Responsibility |
| :--- | :--- |
| `RedBlackTree.h` | The header file defining the `RedBlackBST` interface, nested `BSTNode` structure, and `NodeColor` enum. |
| `RedBlackTree.cc` | The source file containing rebalancing rules, subtree transplanting, rotations, insertion/deletion fixups, and BFS printer. |
| `main.cc` | Main entry point driving operations with a customized dataset. |
| `Makefile` | Build configuration file to compile the project under standard compiler warnings. |
| `README.md` | Detailed analysis and description of the BST structures. |

---

## 3. Building & Running the Code

Use the provided makefile targets to compile and execute:

```bash
# Compile and build the target binary (bst_runner)
make

# Run the compiled binary
make execute

# Clear compiled objects
make clean
```

If compiling manually:
```bash
g++ -std=c++17 -Wall -Werror -O3 -o bst_runner main.cc RedBlackTree.cc
./bst_runner
```

---

## 4. Expected Console Logs

Upon running `bst_runner`, the following output is generated:

```text
Adding items to the tree: 14 22 38 11 29 6 2 8 44 32 

Red-Black Tree BFS representation (R=Red, B=Black):
[22B, 11R, 38R, 6B, 14B, 29B, 44B, 2R, 8R, null, null, null, 32R]

lookup(11) -> Found
lookup(29) -> Found
lookup(99) -> Missing

Erasing items: 22, 6, 38 ...
Red-Black Tree BFS representation after erasure:
[29B, 11R, 44B, 8B, 14B, 32R, null, 2R]

lookup(22) -> Missing
lookup(14) -> Found
```

### Visual Structure (Initial Insertions)
```text
                  22B
                /     \
             11R       38R
            /   \     /   \
          6B    14B  29B   44B
         /  \              /
        2R   8R          32R
```

### Visual Structure (Post-Erasures)
```text
                  29B
                /     \
             11R       44B
            /   \     /
          8B    14B  32R
         /
        2R
```

---

## 5. Main Methods and APIs

```cpp
class RedBlackBST {
public:
    RedBlackBST();
    ~RedBlackBST();

    bool lookup(int data);       // Searches for a value, returning true/false
    void add(int data);          // Inserts a new value and maintains balanced constraints
    void erase(int data);        // Removes a value, performing repair routines if necessary
    void displayBFS();           // Prints the tree state level-by-level (LeetCode BFS layout)
};
```

---

## 6. Structural Constraints & Balancing Rules

To qualify as a valid Red-Black Tree, five critical constraints must be satisfied:
1. **Property 1:** Every node is either Red or Black.
2. **Property 2:** The root of the tree is always Black.
3. **Property 3:** Every leaf (represented by the sentinel `NIL_LEAF`) is Black.
4. **Property 4:** If a node is Red, both of its children must be Black. (No two Red nodes can be adjacent).
5. **Property 5:** Every path from a node to any descendant leaf contains the same number of Black nodes.

---

## 7. Balancing Scenarios on Insertion

When a new item is inserted, it is initially colored **Red**. If its parent is also **Red**, it violates Property 4. To resolve this, `repairInsert(node)` dispatches one of the following scenarios:

### Scenario 0: Parent is Black
If the parent of the inserted node is Black, no invariants are violated.
* **Resolution:** Return immediately.

### Scenario 1: Uncle is Red
If both the parent and the uncle of the node are Red:
* **Resolution:** Recolor the parent and uncle to Black, set the grandparent to Red, and recursively call `repairInsert` on the grandparent.

### Scenario 2: Uncle is Black (Inner Grandchild)
If the uncle is Black, and the node forms a zig-zag line (e.g., node is a right child but parent is a left child):
* **Resolution:** Rotate the parent to align it into Scenario 3, and call `repairInsert` on the parent node.

### Scenario 3: Uncle is Black (Outer Grandchild)
If the uncle is Black, and the node is aligned in a straight line (e.g., node is a left child and parent is a left child):
* **Resolution:** Recolor the parent to Black and the grandparent to Red. Then perform a rotation on the grandparent to balance the subtree.

---

## 8. Balancing Scenarios on Deletion

If a deleted node (or its successor) was **Black**, it reduces the black height of a subtree, violating Property 5 (known as a double-black deficit). To resolve this, `repairDelete(node)` is executed, handling the following configurations:

* **Case A: Sibling is Red**
  * *Fix:* Recolor sibling Black, parent Red. Rotate parent. This transitions into Case B, C, or D.
* **Case B: Sibling is Black, and both of its children are Black**
  * *Fix:* Recolor sibling Red, moving the double-black deficit up to the parent. Recursively resolve.
* **Case C: Sibling is Black, sibling's far child is Black**
  * *Fix:* Recolor sibling's near child Black, sibling Red. Rotate sibling, transitioning into Case D.
* **Case D: Sibling is Black, sibling's far child is Red**
  * *Fix:* Color sibling with parent's original color, color parent and sibling's far child Black. Rotate parent, resolving the deficit.

---

## 9. Cleanup of Allocated Objects

To prevent memory leaks, a recursive post-order traversal (`freeTree(node)`) deallocates every node starting from the root. Additionally, the shared `NIL_LEAF` sentinel node is deleted when the destructor runs.
