Lab 6 — B-Tree Implementation

Course: Advanced DBMS 
Author: Abhiroop Sistu
Roll No: 10287
Language: C++17

This project is a from-scratch implementation of a B-Tree. B-Trees are the foundational data structure for disk-based storage and relational databases (like MySQL, PostgreSQL, and Oracle). By allowing nodes to have up to 2t children and holding multiple keys per node, B-trees minimize disk I/O operations and maintain a shallow, perfectly balanced structure.

This program features an interactive command-line interface to perform the four fundamental operations: insert, search, delete, and display.

Table of Contents
1. Files
2. Build & Run
3. Sample Session
4. B-Tree Properties
5. Insertion Strategy
6. Deletion Cases
7. Search & Display
8. Complexity Analysis

Files
File        Purpose
main.cpp    Contains the BTreeNode struct, BTree class, and the CLI menu.
Makefile    Use `make` to compile, `make run` to execute, and `make clean` to clean up.
README.md   Project documentation.

Build & Run

make          # Compiles the executable ./btree
make run      # Compiles and starts the interactive session
make clean    # Deletes the compiled binary

Manual compilation:
g++ -std=c++17 -Wall -Wextra -O2 -o btree main.cpp
./btree

Upon running, you must specify the minimum degree `t` (where t >= 2). This dictates the branching factor of the tree.

Sample Session

Enter minimum degree (t >= 2): 3

(inserting 10, 20, 30, 40, 50, 5, 15, 25, 35, 45)

B-Tree (level-order):
L0:  [30]
L1:  [5, 10, 15, 20, 25]  [35, 40, 45, 50]

B-Tree (inorder): 5 10 15 20 25 30 35 40 45 50

(deleting 30 — predecessor 25 is promoted to root)
L0:  [25]
L1:  [5, 10, 15, 20]  [35, 40, 45, 50]

B-Tree Properties

For any B-Tree with a minimum degree of t >= 2:
1. Every node maintains its keys in strictly ascending sorted order.
2. Every internal node (except the root) must contain between t-1 and 2t-1 keys.
3. A node containing k keys will always have exactly k+1 children.
4. The tree is perfectly balanced—all leaf nodes exist at the exact same depth.
5. Keys in the i-th child node are strictly bounded by the (i-1)-th and i-th keys of the parent node.

Insertion Strategy

Insertion operates strictly top-down. As we traverse from the root to the appropriate leaf, we proactively split any "full" node (a node with exactly 2t-1 keys) we encounter. 

Splitting a node divides its 2t-1 keys into two nodes of t-1 keys each, and pushes the median key up to the parent. Because we split full nodes on the way down, we guarantee that the parent will always have room to receive this median key. If the root itself splits, a new root is created, increasing the height of the tree by 1.

Deletion Cases

Deletion requires ensuring that no node drops below the minimum threshold of t-1 keys. We recursively descend the tree, ensuring every node we visit has at least t keys (except the root). 

Case 1: Leaf Deletion. If the key is in a leaf, we simply remove it.
Case 2: Internal Deletion. If the key is in an internal node, we:
  a. Replace it with its predecessor (if the left child has >= t keys).
  b. Replace it with its successor (if the right child has >= t keys).
  c. Merge the left and right children if both only have t-1 keys, then recursively delete the key from the merged node.
Case 3: Key Not Found Yet. As we descend into a child to find the key, if that child has only t-1 keys, we either borrow a key from an immediate sibling or merge it with a sibling to ensure it has enough keys before we proceed.

Search & Display

* Search: Recursively scans a node's keys. If the target is found, it returns true; otherwise, it navigates to the appropriate child subtree.
* Inorder Display: Prints the tree in sorted order (Left Child -> Key -> Right Child).
* Level-Order Display: Uses a Breadth-First Search (BFS) queue to print the tree layer by layer, visually confirming the B-Tree's structural integrity.

Complexity Analysis

Given n keys and minimum degree t:
* Height (h): O(log_t n)
* Search: O(t * log_t n) CPU time, O(log_t n) Disk I/O
* Insert: O(t * log_t n) CPU time, O(log_t n) Disk I/O
* Delete: O(t * log_t n) CPU time, O(log_t n) Disk I/O