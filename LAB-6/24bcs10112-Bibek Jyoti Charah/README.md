# Lab 6 — B-Tree

**Author:** Bibek Jyoti Charah · **Roll No:** 24bcs10112

A header-only, templated B-tree of minimum degree `t`, supporting **insert**,
**search**, **overwrite**, and **delete** (with node split, borrow, and merge).

## Design

- A node stores its key/value pairs in one sorted `entries` vector; an
  internal node has exactly `entries + 1` child pointers.
- A node keeps between `t-1` and `2t-1` entries (the root may have fewer).
- **Insert** descends from the root, splitting any full child *before*
  stepping into it, so the leaf that receives the new key is never full.
- **Delete** does the mirror: before descending into a child it ensures the
  child has at least `t` entries by borrowing from a sibling or merging two
  siblings. Deleting a key from an internal node replaces it with its
  in-order predecessor/successor. This keeps deletion to a single downward
  pass.

## Files

- `b_tree.hpp` — the `BTree<Key, Value>` template
- `main.cpp` — demo + randomized cross-check against `std::map`
- `CMakeLists.txt`

## Build & run

```bash
cmake -S . -B build && cmake --build build && ./build/b_tree_demo
```

Or directly:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -O2 main.cpp -o b_tree_demo
./b_tree_demo
```

The demo inserts a small key set, prints the tree level-by-level, runs
lookups and an overwrite, deletes several keys, and finally runs 5000 random
insert/delete operations verified key-for-key against `std::map`. The
internal `check()` validates fanout, key order, and equal leaf depth after
every structural change.
