# B-Tree Database Implementation

A template-based B-Tree database implementation in C++ that provides efficient key-value storage and retrieval.

## Overview

This is a generic B-Tree data structure that supports:
- **Template-based keys and values** - Works with any comparable key type and any value type
- **Search operations** - O(log n) search for keys
- **Insert operations** - O(log n) insertion with automatic node splitting
- **B-Tree properties** - Maintains balanced tree structure

## Features

- **Generic Templates**: Works with any key type (int, string, etc.) and any row type
- **Automatic Balancing**: Automatically splits nodes when they become full
- **Efficient Search**: Binary search-like traversal through the tree
- **Node Management**: Proper memory management for dynamic node creation

## Structure

### Core Components

1. **Entry struct** - Stores a key-value pair
2. **BTree struct** - Represents a node in the B-tree with:
   - `keys`: Vector of entries stored in the node
   - `children`: Vector of child pointers
   - `isLeaf`: Boolean indicating if it's a leaf node

3. **DB class** - Main B-tree database class with:
   - `minDegree`: Minimum degree parameter (t)
   - `minKeys`: Minimum number of keys (t-1)
   - `maxKeys`: Maximum number of keys (2t-1)

### Main Methods

- **`DB(size_t degree)`** - Constructor, initializes tree with given degree
- **`Insert(Key key, Row row)`** - Inserts a key-value pair
- **`Search(Key key)`** - Searches for a key and returns the entry
- **`SearchRecursive(Key key, BTree *node)`** - Recursive search helper

## Compilation

```bash
mkdir build
cd build
cmake ..
make
```

## Usage Example

```cpp
// Create a B-tree with degree 3
DB<int, std::string> database(3);

// Insert key-value pairs
database.Insert(10, "Row10");
database.Insert(20, "Row20");
database.Insert(5, "Row5");
database.Insert(25, "Row25");

// Search for a key
auto result = database.Search(20);
if (result.key != 0) {
    std::cout << "Found: " << result.row << std::endl;
}
```

## B-Tree Properties

- All leaves are at the same level
- For a node with degree `t`:
  - Each node has at most `2t-1` keys
  - Each non-leaf node has at least `t` children (except root)
  - Each non-leaf node has at most `2t` children
  - All keys in a node are sorted in ascending order

## Time Complexity

- **Search**: O(log n)
- **Insert**: O(log n)
- **Space**: O(n)

## Notes

- The current implementation focuses on core operations
- Further enhancements could include delete operations, range queries, and iterator support
