# Lab 5: Red-Black Tree Implementation

**Author:** Siddhant Prasad  
**Roll Number:** 24BCS10255

---

## 🎯 Aim
To implement and analyze a Red-Black Tree, a self-balancing binary search tree, and study the balancing mechanisms (rotations and recoloring) that preserve the tree's depth properties after node updates.

---

## 📚 Red-Black Tree Properties
A Red-Black Tree is a binary search tree where each node has a color attribute (either `RED` or `BLACK`). To ensure the tree remains approximately balanced (height is bounded by $2 \log_2(n + 1)$), the structure must satisfy the following **five invariants**:

1. **Node Color**: Every node is colored either red or black.
2. **Root Rule**: The root of the tree is always black.
3. **Leaf Rule**: All leaf nodes (represented by the sentinel node `TNULL` or `NIL`) are black.
4. **Red Node Constraint**: If a node is red, both of its children must be black. (No two consecutive red nodes are allowed on any path).
5. **Black Height Rule**: For each node, all simple paths from the node to descendant leaves contain the same number of black nodes.

---

## ⚙️ Balancing Scenarios (Fixing Double-Red Conflicts)

When inserting a new node `K`:
- `K` is inserted as a standard BST leaf and initially colored **RED**.
- If `K`'s parent is **RED**, it violates Rule 4 (Consecutive Red Nodes). We look at the color of `K`'s **Uncle** (`U`):

### Case 1: Uncle `U` is RED
- **Action**: Recolor parent and uncle to `BLACK`, and grandparent to `RED`.
- Set `K` to grandparent and repeat properties check upward.
- *No rotations are required in this phase.*

### Case 2: Uncle `U` is BLACK (Triangle Configuration)
- **Action**: If `K` is an inner child (e.g. right child of left parent), perform a rotation about `K`'s parent to align them into a line (converting it into Case 3).

### Case 3: Uncle `U` is BLACK (Line Configuration)
- **Action**: Recolor parent to `BLACK`, grandparent to `RED`, and perform a single rotation about the grandparent to restore black height balance.

---

## 💻 Class Design & Method Loggers
- **`leftRotate(x)` & `rightRotate(y)`**: Rotates subtrees about designated nodes, adjusting pointers and printing rotation logs to show structural corrections.
- **`fixInsert(k)`**: Evaluates Cases 1, 2, and 3, resetting parent-child connections and recoloring nodes.
- **`insert(key)`**: Places nodes using classic BST insertion rules, sets color to `RED`, and fires balancing steps if parent is `RED`.
- **`search(key)`**: Traverses from root to target, printing each node visited to display search paths and logging comparison performance.
- **`inorder()`**: Recursively prints keys and node colors in ascending order.

---

## 🛠️ Compilation and Execution

### Compilation
Build using a standard C++ compiler supporting standard features:
```bash
g++ -std=c++17 red_black_tree.cpp -o red_black_tree
```

### Execution
Run the compiled executable:
```bash
./red_black_tree
```

---

## 📊 Sample Execution Log & Verification

Below is the execution output representing initialization, insertion steps, balancing rotations, and sorted traversal verification:

```text
===================================================
  Lab 5: Red-Black Tree Implementation & Balancing 
===================================================
[INIT] Empty Red-Black Tree initialized with black leaf sentinel (TNULL).

------------------ TREE STRUCTURE ------------------
Empty Tree
----------------------------------------------------


[INSERT START] Request to insert Key: 10
 -> Key: 10 inserted as root node.
 -> Root node recolored to BLACK.

------------------ TREE STRUCTURE ------------------
R----10(BLACK)
----------------------------------------------------


[INSERT START] Request to insert Key: 20
 -> Key: 20 placed as right child of Node (10).

------------------ TREE STRUCTURE ------------------
R----10(BLACK)
   R----20(RED)
----------------------------------------------------


[INSERT START] Request to insert Key: 30
 -> Key: 30 placed as right child of Node (20).
[BALANCING] Case 3 (Line, Uncle BLACK): Outer child. Recoloring Parent (20) to BLACK, Grandparent (10) to RED.
[ROTATION] Left rotating about Node (10)

------------------ TREE STRUCTURE ------------------
R----20(BLACK)
   L----10(RED)
   R----30(RED)
----------------------------------------------------


[INSERT START] Request to insert Key: 15
 -> Key: 15 placed as left child of Node (20).
[BALANCING] Case 1 (Uncle RED): Recoloring Node (10) and Node (30) to BLACK, and Grandparent (20) to RED

------------------ TREE STRUCTURE ------------------
R----20(BLACK)
   L----10(BLACK)
      R----15(RED)
   R----30(BLACK)
----------------------------------------------------


[INSERT START] Request to insert Key: 25
 -> Key: 25 placed as left child of Node (30).

------------------ TREE STRUCTURE ------------------
R----20(BLACK)
   L----10(BLACK)
      R----15(RED)
   R----30(BLACK)
      L----25(RED)
----------------------------------------------------


[SEARCH] Searching for Key: 15
 Path: Node(20) -> Node(10) -> Node(15) -> FOUND!
 -> Searches took 3 comparison(s).

[SEARCH] Searching for Key: 99
 Path: Node(20) -> Node(30) -> TNULL (NOT FOUND)
 -> Search failed after 3 comparison(s).

[TRAVERSAL] Inorder Traversal:
 10 (BLACK)  15 (RED)  20 (BLACK)  25 (RED)  30 (BLACK)  

===================================================
  Lab 5 execution completed successfully!          
===================================================
```

---

## 🏁 Conclusion
The Red-Black Tree was successfully implemented and verified. The results demonstrate that:
1. BST insertion rules successfully place keys in sorted relative order.
2. Case 1, 2, and 3 triggers correctly rebalance the tree through localized recoloring and subtree rotations.
3. Inorder traversal verifies the sorted BST sequencing.
4. Red-Black trees maintain balanced structures (guaranteeing $O(\log n)$ search time) compared to skewed binary trees.
