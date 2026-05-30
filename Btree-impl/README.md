## Introduction

A B-Tree is a balanced multi-way search tree commonly used in indexing systems, databases, and storage engines. 
Unlike binary trees, a B-Tree allows multiple keys inside a node which helps reduce traversal time.

### Characteristics of B-Tree

For a minimum degree `t`:

- Each node can contain at most `2t - 1` keys.
- Each internal node can have up to `2t` children.
- Keys remain sorted inside every node.
- All leaf nodes stay at the same level, ensuring balance.

## Features Implemented

1. Insert integer values into the B-Tree.
2. Search elements in the tree.
3. Display in-order traversal.
4. Print tree structure visually.
5. Demo execution using sample values.

## Compilation

```bash
make
```

## Execute Program

```bash
make run
```

or

```bash
./btree_app
```

## Clean Build Files

```bash
make clean
```