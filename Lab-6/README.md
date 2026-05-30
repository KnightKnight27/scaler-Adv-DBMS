# Lab 6 - B-Tree Implementation

## Student Details

**Name:** Srujan Gowda KS
**Roll Number:** 24BCS10339

## Objective

The objective of this lab is to implement a B-Tree data structure in C++ using templates. The program supports insertion and search operations while maintaining the properties of a B-Tree through node splitting.

## Files

| File      | Description                           |
| --------- | ------------------------------------- |
| main.cpp  | B-Tree implementation using templates |
| README.md | Documentation and usage details       |

## Features

* Generic template-based implementation
* Search operation
* Insert operation
* Automatic node splitting
* Tree display function
* Handles root splitting when required

## Build and Run

```bash
g++ -std=c++17 main.cpp -o btree
./btree
```

## Sample Output

```text
B-Tree Structure:
10 20
5 6 7
12 17
30

Search 12: Found
Search 25: Not Found
```

## Time Complexity

| Operation | Complexity |
| --------- | ---------- |
| Search    | O(log n)   |
| Insert    | O(log n)   |
| Display   | O(n)       |

## Conclusion

The B-Tree was successfully implemented using C++ templates. The program supports insertion and searching while maintaining balanced tree properties through node splitting.
