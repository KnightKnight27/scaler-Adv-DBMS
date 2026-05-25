# LAB-5: Red-Black Tree Implementation in C++

## Aim

This lab implements a Red-Black Tree in C++17 and demonstrates how self-balancing binary search trees keep insertion and lookup efficient by using recoloring and rotations.

## Files Included

- `red_black_tree.cpp`: Complete C++17 program containing the `RedBlackTree` class, insertion logic, search, traversals, rotations, validation checks, and a demonstration `main()` function.
- `README.md`: Brief explanation of the experiment and instructions to compile and run it.

## Red-Black Tree Properties

A valid Red-Black Tree follows these rules:

1. Every node is either red or black.
2. The root must always be black.
3. A red node cannot have a red child.
4. Every path from a node to a null leaf must contain the same number of black nodes.
5. The structure must still satisfy ordinary Binary Search Tree ordering.

## How Insertion Works

New values are inserted the same way as in a normal Binary Search Tree, with the new node initially colored red. If this creates a rule violation, the tree is repaired using the standard Red-Black Tree fix-up cases:

- recoloring when the parent and uncle are red,
- a rotation to convert a triangle into a straight line,
- a final rotation around the grandparent after recoloring.

The program includes both left rotation and right rotation helpers because the balancing cases are symmetric.

## Compilation

```bash
g++ -std=c++17 red_black_tree.cpp -o red_black_tree
```

## Execution

```bash
./red_black_tree
```

## Expected Output

The program demonstrates the tree with a fixed insertion sequence and prints:

- the inorder traversal,
- the level order traversal with node colors,
- search results for one present key and one absent key,
- the result of attempting to insert a duplicate key,
- the final validation message showing whether the tree satisfies Red-Black Tree properties.

## Notes

The implementation includes a validation routine that checks the root color, red-parent violations, equal black height on all root-to-leaf paths, and BST ordering. This makes the output useful not only for demonstration but also for confirming that the balancing logic is working correctly.
