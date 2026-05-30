# B-tree lab

Minimal B-tree program in C++ with interactive menu.

Operations: insert, delete, search, show inorder, show levels.

Build and run:

```bash
make        # builds ./btree
make run    # run interactive program
make test   # run the basic test suite
```

The program asks for minimum degree t (>= 2). Run the menu to exercise the tree.

## Operations Overview

- **Search**: Starts at the root, compares keys, and recursively navigates down matching child pointers until the target is found or a leaf is fully traversed (returns not found).
- **Insert**: Traverses to the appropriate leaf to place the key. If any node along the path is full (2t - 1 keys), it preemptively splits into two nodes and pushes the middle key up to its parent to keep the tree balanced.
- **Delete**: Removes the target key. If it's in an internal node, it's replaced by a predecessor or successor. As it traverses down, if nodes lack enough keys (below t - 1), it borrows from siblings or merges nodes to maintain the B-tree properties.
