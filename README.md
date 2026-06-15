# Lab 6 — Clean B-Tree Demo

This folder contains a header-only templated B-tree and a demo driver that exercises:

- insert
- search
- overwrite
- erase
- in-order traversal
- `verify()` invariant checks
- a 5,000-operation randomized stress test against `std::map`

## Files

- `b_tree.hpp`
- `main.cpp`
- `CMakeLists.txt`
- `README.md`

## Build

```bash
cmake -S . -B build
cmake --build build
./build/b_tree_demo
```

## Direct compile

```bash
clang++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -O2 main.cpp -o b_tree_demo
./b_tree_demo
```

## Notes

This version keeps the same Lab 6 idea but presents it in a cleaner structure:
the tree logic is isolated in `b_tree.hpp`, the demo lives in `main.cpp`,
and the README is fixed so the Markdown fences render correctly.