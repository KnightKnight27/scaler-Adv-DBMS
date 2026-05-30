# Lab 5 — Red-Black Tree (C++17)

A generic, Red-Black Tree implementation in C++17 using templates and abstract interfaces.

---

## Assignment Purpose

Implement a self-balancing Binary Search Tree using the Red-Black Tree algorithm. The implementation must be generic (works with any comparable type), support insert, delete, search, and in-order traversal.

---

## File Structure

```
.
├── IComparator.h        # Abstract comparator interface (Dependency Inversion)
├── DefaultComparator.h  # Default comparator using operator<
├── ITree.h              # Abstract tree interface (Interface Segregation)
├── RBNode.h             # Node structure holding data, color, and links
├── RedBlackTree.h       # Core Red-Black Tree implementation
├── main.cc              # Driver: demos with int, string, and custom struct
└── CMakeLists.txt       # CMake build configuration (C++17)
```

---

## File Descriptions

| File | Responsibility |
|---|---|
| `IComparator.h` | Pure abstract interface with a single `compare(a, b)` method |
| `DefaultComparator.h` | Concrete comparator for any type that defines `operator<` |
| `ITree.h` | Pure abstract interface declaring `insert`, `remove`, `contains`, `inorder` |
| `RBNode.h` | Plain struct holding `data`, `color` (Red/Black), and `parent/left/right` pointers |
| `RedBlackTree.h` | Full RBT implementation: rotations, insert fixup, delete fixup |
| `main.cc` | Demonstrates usage with `int`, `std::string`, and a custom `Employee` struct |
| `CMakeLists.txt` | Builds target `rbt` with C++17 standard |

---

## Build & Run

```bash
mkdir build && cd build
cmake ..
make
./rbt
```

Or directly:

```bash
g++ -std=c++17 -O2 -o rbt main.cc && ./rbt
```

---

## Supported Types

Any type that supports `operator<` works out of the box via `DefaultComparator`.  
For custom ordering, implement `IComparator<T>` and pass it to the constructor:

```cpp
RedBlackTree<int> tree;                                          // default
RedBlackTree<int> tree(std::make_unique<ReverseComparator>());  // custom
```