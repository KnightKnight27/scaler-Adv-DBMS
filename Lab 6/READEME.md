# Lab 6 — B-Tree in C++

An interactive B-tree implementation supporting:

- insertion
- search
- inorder traversal

The insertion algorithm uses the standard top-down preemptive split approach:
any full child encountered while descending is split before recursion continues.

---

## Build

```bash
g++ main.cpp -o main