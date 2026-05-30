# Lab 6: B+ Tree Implementation

**Name:** Ankit Kumar  
**Roll No:** 24BCS10189  
**Lab No:** 6

## Objective

Implement insertion and search in a B+ Tree. The implementation stores all
data keys in linked leaf nodes and keeps separator keys in internal nodes.

## Files

| File | Purpose |
| --- | --- |
| `main.cpp` | B+ Tree implementation and demo |

## Features

- Order `4` B+ Tree.
- Sorted insertion into leaf nodes.
- Leaf node splitting.
- Internal node splitting.
- Root splitting when the old root overflows.
- Search using internal separator keys.
- Linked leaf chain for ordered traversal.

## Build and Run

```bash
cd lab6
c++ -std=c++17 -Wall -Wextra -Wpedantic main.cpp -o lab6
./lab6
```

## Sample Output

```text
B+ Tree level order:
[10]
[7] [20 30]
[3 5 6] [7] [10 12 17] [20 25] [30 40 50]
Leaf chain:
[3 5 6] -> [7] -> [10 12 17] -> [20 25] -> [30 40 50] -> NULL
Search 17: found
Search 99: not found
```

## Conclusion

The B+ Tree keeps records in sorted leaf nodes and uses internal nodes only
for routing. Splitting keeps each node within the allowed key capacity and
the linked leaf chain supports efficient range scans.
