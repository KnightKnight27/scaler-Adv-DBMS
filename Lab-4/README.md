# Lab 4 — Red-Black Tree & Full B-Tree

This lab has two parts, matching the official lab definition
(*"Lab Session 4: Red-Black Tree & Full B-Tree in C++"*).

| Part | Topic | Folder |
| ---- | ----- | ------ |
| Part 1 | Red-Black Tree (self-balancing BST) | [`Part1-RBT/`](Part1-RBT/) |
| Part 2 | Full B-Tree (on-disk style index, order `t`) | [`Part2-BTree/`](Part2-BTree/) |

Both are written in C++17 and self-check their invariants after every
operation. See each part's own README for the algorithm details, build
instructions, and demo output.

## Why these two together

A Red-Black Tree and a B-Tree are both balanced search trees, but they
target different storage:

- **Red-Black Tree** keeps one key per node — great for *in-memory*
  sorted maps (this is what `std::map` uses internally).
- **B-Tree** packs many keys into one wide node so that **one node = one
  disk page**. Databases (PostgreSQL, MySQL/InnoDB, SQLite) use B-Trees
  (really B+Trees) for on-disk indexes because the cost that matters on
  disk is the *number of page reads*, and a wide node means a very short
  tree.

Studying them side by side makes the trade-off concrete: same O(log n)
goal, very different node shape, driven entirely by where the data lives.
