# B-Tree

Project Files

File	Description

- `BTree.hpp` 	B-Tree implementation (insert, split, borrow, merge, delete) and helpers.
- `main.cpp` 	Driver demonstrating inserts and deletes.
- `Makefile` 	Build script using `g++` with C++17.
- `README.md` 	This file.

Build

```bash
cd b-tree
make
```

Run

```bash
./b_tree
```

Notes

- The implementation uses a minimum degree `t` (set in `main.cpp`).
- The demo prints the B-Tree contents after inserts and after some deletes.
