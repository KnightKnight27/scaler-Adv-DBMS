# Lab 5: Red-Black Tree Implementation

**Name:** Ankit Kumar  
**Roll No:** 24BCS10189  
**Lab No:** 5

## Objective

Implement a Red-Black Tree in C++ using standard DSA concepts: binary search
tree insertion, rotations, recoloring, and insertion fix-up.

## Files

| File | Purpose |
| --- | --- |
| `main.cpp` | Red-Black Tree implementation and demo |

## Red-Black Tree Properties

1. Every node is either red or black.
2. The root is always black.
3. Null leaves are treated as black.
4. A red node cannot have a red child.
5. Every path from a node to a null leaf has the same number of black nodes.

## Operations Implemented

| Operation | Description | Complexity |
| --- | --- | --- |
| `insert` | Inserts a key and restores Red-Black properties | `O(log n)` |
| `search` | Finds whether a key exists | `O(log n)` |
| `rotateLeft` | Performs left rotation during balancing | `O(1)` |
| `rotateRight` | Performs right rotation during balancing | `O(1)` |
| `inorder` | Prints keys in sorted order with colors | `O(n)` |

## Build and Run

```bash
cd lab5
c++ -std=c++17 -Wall -Wextra -Wpedantic main.cpp -o lab5
./lab5
```

## Sample Output

```text
Inorder traversal:
1(R) 5(B) 10(R) 15(B) 20(B) 25(B) 30(R) 50(R) 60(B) 70(R)
Level order traversal:
20(B) 10(R) 30(R) 5(B) 15(B) 25(B) 60(B) 1(R) 50(R) 70(R)
Search 25: found
Search 99: not found
```

## Conclusion

The implementation maintains Red-Black Tree invariants after each insertion
using recoloring and rotations. This keeps the tree height logarithmic, so
search and insertion remain efficient.
