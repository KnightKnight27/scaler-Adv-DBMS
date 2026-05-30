# B-Tree Implementation in C++

A templated B-Tree data structure supporting insertion, search, and display.

## Features

- Generic template support for any comparable key type
- Configurable minimum degree `t`
- Automatic node splitting on overflow
- Recursive search and level-order display

## Usage

```cpp
BTree<int> tree(3);   // degree = 3

tree.insert(10);
tree.insert(20);
tree.insert(5);

tree.print();                          // Display tree structure
bool found = tree.search(12);          // Returns true/false
```

## Key Operations

| Operation | Description | Time Complexity |
|-----------|-------------|-----------------|
| `insert(key)` | Inserts a key, splitting nodes as needed | O(log n) |
| `search(key)` | Returns true if key exists | O(log n) |
| `print()` | Displays all nodes level by level | O(n) |

## Build & Run

```bash
g++ -std=c++17 -o btree main.cpp
./btree
```

## Example Output

```
B-Tree structure:
10 20
5 6 7
12 17
30

Search 12: Found
```