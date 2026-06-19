# Lab 6 - B-Tree in C++

This project implements a B-Tree in C++ with support for:

* Inserting keys
* Searching for keys
* Displaying keys using inorder traversal

The tree is built using the standard B-Tree insertion algorithm, where full nodes are split before insertion continues. The minimum degree (`t`) is provided by the user at runtime.

## How to Run

```bash
g++ main.cpp -o main
./main
```

## Sample Test

Minimum degree:

```text
3
```

Insert:

```text
10 20 5 6 12 30 7 17
```

Search:

```text
12
```

Output:

```text
Key is present in the tree.
```

Inorder Traversal:

```text
5 6 7 10 12 17 20 30
```

This implementation demonstrates how B-Trees maintain balanced structure while supporting efficient insertion and search operations.
