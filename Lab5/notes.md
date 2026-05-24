# Lab 5: Red-Black Tree — Notes

**Name:** Siddhanth Kapoor
**Roll Number:** 10154

`main.cpp` implements a Red-Black Tree over integers with insertion, search,
inorder traversal, and a property verifier. Build with the included CMake or:

```bash
c++ -std=c++17 -O2 main.cpp -o rbtree && ./rbtree
```

## Red-Black properties maintained

1. Every node is red or black.
2. The root is black (`insertFixup` ends with `root->color = BLACK`).
3. Every nil leaf is black (single shared `nil` sentinel).
4. A red node never has a red child.
5. Every root-to-leaf path has the same number of black nodes (black-height).

## Task mapping

- **Initialization:** the tree starts as just the black `nil` sentinel (`root = nil`).
- **Insertion:** `insert` places the node by BST rules, paints it red, then calls `insertFixup`.
- **Balancing:** `insertFixup` handles the three cases — (1) red uncle → recolour and move up; (2) triangle → rotate to a line; (3) line → recolour parent/grandparent and rotate. `leftRotate`/`rightRotate` do the structural changes.
- **Search:** `search` walks down comparing keys and prints the path; balanced height keeps this O(log n).
- **Traversal:** `inorder` prints keys in sorted order with their colours.
- **Verification:** `verifyProperties` checks root-black, no red-red, and equal black-height; it is called after **every** insertion in the demo, and all pass.

## Observed run

Inserting `10 20 30 15 25 5 1 40 35 50`, the black-height grows from 2 to 3 as the
tree fills, and the verifier reports "properties hold" after each step. The inorder
output `1 5 10 15 20 25 30 35 40 50` confirms BST ordering is preserved.
