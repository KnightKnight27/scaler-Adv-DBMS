# Lab 6 — B-Tree Implementation (C++17)

**Name:** Pratham Onkar Singh
**Roll No.:** 24bcs10136

---

## Overview

This project implements a generic header-only B-Tree container:

```cpp
adbms::b_tree<Key, Value, Compare>
```

The tree behaves as an ordered key-value map and is parameterized by the B-Tree minimum degree `t`.

The insertion logic follows the standard CLRS top-down split strategy, while deletion implements the complete CLRS Chapter 18 removal algorithm, including predecessor, successor, borrowing, and merge operations.

A built-in verification routine continuously checks structural correctness, and a randomized stress-testing framework compares the implementation against `std::map` to ensure correctness under thousands of operations.

---

## Why B-Trees Matter in Databases

Database systems rarely use binary search trees as their primary index structure.

Instead, engines such as SQLite, PostgreSQL, and MySQL typically rely on B-Trees (or B+ Trees) because each node stores many keys rather than just one. This dramatically reduces tree height and therefore minimizes expensive disk-page accesses.

The B-Tree implemented here mirrors the logical structure used in those systems, except that nodes are stored in memory rather than on disk pages.

---

## Project Structure

```text
lab6/
├── b_tree.hpp
├── main.cpp
├── CMakeLists.txt
└── README.md
```

### File Descriptions

| File             | Purpose                                      |
| ---------------- | -------------------------------------------- |
| `b_tree.hpp`     | Complete header-only B-Tree implementation   |
| `main.cpp`       | Demonstration program and stress-test driver |
| `CMakeLists.txt` | Build configuration using CMake              |
| `README.md`      | Documentation and design discussion          |

---

## Building the Project

### Using CMake

```bash
cmake -S . -B build
cmake --build build
./build/b_tree_demo
```

### Direct Compiler Invocation

```bash
clang++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o b_tree_demo
./b_tree_demo
```

The code was tested using Apple Clang (C++17) and compiles without warnings.

Expected final output:

```text
All B-Tree checks passed.
```

---

# B-Tree Fundamentals

Unlike a Red-Black Tree, where each node contains a single key and two child pointers, a B-Tree stores multiple keys inside every node.

For minimum degree `t`:

* Maximum keys per node = `2t − 1`
* Minimum keys per non-root node = `t − 1`
* Maximum children = `2t`

A larger branching factor results in significantly smaller height.

For example:

| Structure         | Approximate Height (1,000,000 Keys) |
| ----------------- | ----------------------------------- |
| Red-Black Tree    | ~40                                 |
| B-Tree (`t = 50`) | ~4                                  |

Fewer levels translate directly into fewer page reads when used as a database index.

---

# B-Tree Properties Checked by `verify()`

The implementation validates all major B-Tree requirements.

### 1. Key Count Constraints

Every non-root node contains:

```text
t − 1 ≤ keys ≤ 2t − 1
```

The root is allowed to contain fewer keys.

### 2. Child Relationship

For a node containing `k` keys:

```text
children = k + 1
```

### 3. Sorted Keys

Keys within every node must remain strictly ordered.

### 4. Separator Ordering

For parent keys:

```text
K1 < K2 < ... < Kn
```

all child subtrees must remain within their corresponding key ranges.

### 5. Uniform Leaf Depth

Every leaf must appear at exactly the same depth.

This property guarantees balance.

---

# Search Operation

Searching begins at the root and scans keys inside the current node.

Possible outcomes:

1. Key found → return result.
2. Key smaller than separator → descend left.
3. Key larger than separator → descend right.
4. Leaf reached without match → key absent.

The implementation uses a helper that finds the first position whose key is not less than the target.

Complexity:

```text
O(t · log_t n)
```

---

# Insertion Strategy

Insertion follows the standard proactive-split approach.

Whenever the algorithm is about to descend into a full child:

```text
2t − 1 keys
```

the child is split before continuing.

This guarantees that the destination leaf always has available space.

Benefits:

* No upward repair pass required.
* Height increases only when the root splits.
* Simpler insertion logic.

---

# Deletion Strategy

Deletion is considerably more involved than insertion.

Before descending into a child, the algorithm guarantees that the child contains at least `t` keys.

This prevents underflow after removal.

The implementation handles all CLRS deletion scenarios.

### Removing From a Leaf

The key is erased directly.

### Removing From an Internal Node

Three possibilities exist:

1. Replace with predecessor.
2. Replace with successor.
3. Merge adjacent children and recurse.

### Descending Cases

Before descending:

* Borrow from left sibling.
* Borrow from right sibling.
* Merge siblings if both are minimal.

All cases described in CLRS Section 18.3 are implemented.

---

# Root Shrinking

Tree height can decrease only after deletion.

If the root becomes empty and still has a child:

```text
root
 |
child
```

the child becomes the new root.

This is the inverse of root splitting during insertion.

---

# Public Interface

```cpp
adbms::b_tree<int, std::string> tree(3);

tree.insert(42, "movie");
tree.contains(42);

tree.at(42);

tree.erase(42);

tree.size();
tree.empty();
tree.degree();

tree.print();

tree.in_order(
    [](int k, const std::string& v) {
        // ...
    }
);

auto err = tree.verify();
```

---

# Demonstration Program

The supplied driver performs several scenarios.

## Scenario 1

Small tree with movie titles.

Demonstrates:

* insertion
* printing
* sorted traversal

---

## Scenario 2

Lookup functionality.

Demonstrates:

* `contains`
* `at`
* overwrite behavior
* exception handling

---

## Scenario 3

Deletion coverage.

Exercises:

* leaf deletion
* internal deletion
* merge operations
* root collapse

---

## Scenario 4

Sequential insertion into a degree-2 B-Tree.

Demonstrates that ordered input remains balanced.

---

## Scenario 5

50,000 random insertions into a large-degree tree.

Used to illustrate the height advantages of B-Trees.

---

## Scenario 6

Randomized validation test.

Configuration:

```text
5000 operations
```

Reference structure:

```cpp
std::map<int,int>
```

After each step the program verifies:

* identical size
* matching search results
* valid B-Tree invariants

The final in-order traversal must match `std::map`.

---

# Complexity Analysis

| Operation    | Complexity     |
| ------------ | -------------- |
| Insert       | O(t · log_t n) |
| Search       | O(t · log_t n) |
| Erase        | O(t · log_t n) |
| Traversal    | O(n)           |
| Verification | O(n)           |

For practical database values of `t`, the dominant factor becomes the reduced tree height rather than the per-node scan.

---

# Differences From the Reference Implementation

| Area           | Reference          | This Project                   |
| -------------- | ------------------ | ------------------------------ |
| Container Type | Integer set        | Generic key-value map          |
| Deletion       | Not implemented    | Full CLRS deletion             |
| Layout         | Single source file | Header-only library            |
| Verification   | Minimal            | Full invariant checker         |
| Testing        | Limited            | 5000-op randomized oracle test |
| Build System   | Compiler command   | CMake configuration            |

Although insertion follows the same CLRS algorithm, this implementation extends functionality substantially through deletion support, validation tooling, generic typing, and large-scale testing.

---

# Quick Commands

```bash
cmake -S . -B build
cmake --build build
./build/b_tree_demo
```

or

```bash
clang++ -std=c++17 -Wall -Wextra -Wpedantic -O2 main.cpp -o b_tree_demo
./b_tree_demo
```

Successful execution should end with:

```text
All B-Tree checks passed.
```
