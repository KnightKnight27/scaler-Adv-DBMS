# Red-Black Tree Implementation in C++

## Objective

The objective of this lab is to implement a Red-Black Tree in C++ using only Data Structures and Algorithms concepts.

This implementation focuses on:

* Binary Search Tree insertion
* Red-Black Tree balancing
* Left and right rotations
* Recoloring operations
* Maintaining balanced tree height
* Efficient insertion and search operations

No threading, synchronization, or mutex logic is used in this implementation.

---

# Introduction to Red-Black Tree

A Red-Black Tree is a self-balancing Binary Search Tree.

It ensures that the tree remains approximately balanced after every insertion operation, which guarantees efficient performance.

Red-Black Trees are commonly used in:

* Databases
* Operating systems
* STL containers such as `map` and `set`
* File systems
* Memory management systems

The balancing mechanism is achieved using:

* Node coloring
* Rotations
* Recoloring rules

---

# Properties of Red-Black Tree

A valid Red-Black Tree follows the following rules:

1. Every node is either RED or BLACK.

2. The root node is always BLACK.

3. Every leaf node (NULL node) is considered BLACK.

4. If a node is RED, both its children must be BLACK.

5. Every path from a node to its descendant NULL nodes must contain the same number of BLACK nodes.

These properties ensure that the height of the tree remains balanced.

---

# Node Structure

Each node in the Red-Black Tree contains:

```cpp
struct Node {
    int data;
    Color color;
    Node* left;
    Node* right;
    Node* parent;
};
```

Explanation:

| Field  | Purpose                |
| ------ | ---------------------- |
| data   | Stores node value      |
| color  | Stores RED or BLACK    |
| left   | Pointer to left child  |
| right  | Pointer to right child |
| parent | Pointer to parent node |

---

# Rotations in Red-Black Tree

Rotations are used to maintain balance after insertion.

Two types of rotations are implemented:

## Left Rotation

Before rotation:

```text
    x
     \
      y
```

After rotation:

```text
      y
     /
    x
```

Left rotation shifts a node downward to the left while moving its right child upward.

---

## Right Rotation

Before rotation:

```text
      y
     /
    x
```

After rotation:

```text
    x
     \
      y
```

Right rotation shifts a node downward to the right while moving its left child upward.

---

# Insertion Process

The insertion process consists of two stages:

## 1. Standard BST Insertion

The new node is inserted according to Binary Search Tree rules.

Rules:

* Smaller values go to the left subtree.
* Larger values go to the right subtree.

New nodes are initially colored RED.

---

## 2. Fixing Red-Black Violations

After insertion, the tree may violate Red-Black properties.

The following operations are used to restore balance:

* Recoloring
* Left rotation
* Right rotation

The balancing logic checks:

* Parent node color
* Uncle node color
* Grandparent relationships

This ensures that the tree remains balanced after every insertion.

---

# Time Complexity

| Operation | Time Complexity |
| --------- | --------------- |
| Search    | O(log n)        |
| Insertion | O(log n)        |
| Rotation  | O(1)            |

Because the tree remains balanced, operations are significantly faster compared to an unbalanced Binary Search Tree.

---

# Program Implementation

The implementation includes:

* Node creation
* Color management
* Left rotation
* Right rotation
* BST insertion
* Fixing insertion violations
* Inorder traversal

The implementation uses classes and pointers in C++.

---

# Sample Insertions

The following values were inserted into the Red-Black Tree:

```cpp
10
20
30
15
25
5
1
```

These insertions trigger:

* Recoloring operations
* Left rotations
* Right rotations

which maintain the balanced structure of the tree.

---

# Sample Output

```text
Inorder Traversal of Red-Black Tree:
1 (RED) 5 (BLACK) 10 (RED) 15 (BLACK) 20 (BLACK) 25 (RED) 30 (BLACK)
```

The output displays:

* Node values
* Node colors

This confirms that the Red-Black Tree properties are maintained correctly.

---

# Advantages of Red-Black Tree

1. Self-balancing structure

2. Efficient insertion and searching

3. Guaranteed logarithmic complexity

4. Prevents degeneration into linked-list structure

5. Suitable for large datasets

---

# Conclusion

This lab successfully implemented a Red-Black Tree in C++ using only Data Structures and Algorithms concepts.

The implementation demonstrated:

* Balanced Binary Search Tree behavior
* Tree rotations
* Recoloring logic
* Red-Black Tree properties
* Efficient insertion operations

The project provided practical understanding of self-balancing trees and their importance in efficient data storage and retrieval systems.
