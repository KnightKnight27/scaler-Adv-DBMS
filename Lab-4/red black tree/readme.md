# Red-Black Tree

Project Files

File	Description

- `red-black-tree.h` 	The class definition, including `Node` struct, `Color` enum, public methods, and private helpers.
- `red-black-tree.cc` 	The implementation of insertion, deletion, rotations, and fixups.
- `main.cc` 	Driver demonstrating inserts, searches, and deletes.
- `Makefile` 	Build script using `g++` with C++17.
- `readme.md` 	This file.

Build

```bash
cd "red black tree"
make
```

Run

```bash
./red_black_tree
```

Notes

- The implementation uses a `nil` sentinel node.
- The API provides `insert`, `remove`, `contains`, and `printInOrder`.
