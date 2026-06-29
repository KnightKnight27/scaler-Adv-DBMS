# Lab 5: Red-Black Tree Implementation

## Objective
The objective of this lab is to implement and analyze a self-balancing Red-Black Tree (RBT) in C++. We demonstrate how recoloring and rotations (left and right) restore tree properties after node insertions, guaranteeing a balanced height and keeping operations efficient.

## Structure
*   `main.cpp`: Contains the Red-Black Tree class implementation and a driver program displaying step-by-step logs for node insertions, structural fixups, and tree operations.
*   `CMakeLists.txt`: Build configuration for compiling the project.
*   `.gitignore`: Ignores CMake build outputs and executable binaries.

## Build and Run Instructions

### Prerequisites
Make sure you have CMake and a C++ compiler installed (e.g. GCC/g++, clang, or MSVC).

### Compilation
From the `Lab 5` directory, run:
```bash
cmake -B build
cmake --build build
```

### Execution
Run the compiled executable:
```bash
# On Linux/macOS
./build/rbt_demo

# On Windows (PowerShell)
.\build\rbt_demo.exe
```

## Programmatic Property Verification
The implementation verifies the following standard Red-Black Tree properties after every insertion:
1. Every node is either RED or BLACK.
2. The root is BLACK.
3. Every leaf (NIL) is BLACK.
4. If a node is RED, then both its children are BLACK (no consecutive red nodes).
5. For each node, all simple paths from the node to descendant leaves contain the same number of black nodes.
6. Binary Search Tree (BST) ordering invariants are maintained.
