# Self-Balancing Red-Black Tree Implementation (C++)

**Course Module:** Data Structures and Algorithms / Advanced Database Management Systems  
**Language Specification:** C++17

---

## Overview

This project provides an efficient, native implementation of a **Red-Black Tree (RBT)** from scratch. As a balanced binary search variant, this tree guarantees steady $O(\log n)$ efficiency constraints for standard operations including structural searching, element insertion, and leaf removal.

The architectural logic within this program maps directly to the standard patterns popularized by the CLRS (*Introduction to Algorithms*) handbook. To maintain cleanliness across code pathways and avoid troublesome null checks, our tree architecture integrates a standalone **Sentinel Node (`nilNode`)** acting as unified mock leaf elements.

---

## Source Directory Information

| Component File | Role & Assignment Scope |
| :--- | :--- |
| `RedBlackTree.cpp` | Full application logic containing data node definitions, tree rotation routines, insertion correction checks, and text-based tree terminal printouts. |
| `README.md` | Formal design specifications, rules documentation, and runtime performance overviews. |

---

## Compilation & Execution

To compile the implementation using the standard GNU C++ Compiler (GCC), run the following statements in your command console:

```bash
# Build executable using C++17 standard
g++ -std=c++17 -Wall -Wextra -O2 -o rbtree_exec RedBlackTree.cpp

# Run the generated application binary
./rbtree_exec