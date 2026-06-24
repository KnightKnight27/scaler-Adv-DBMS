# Lab 6: B-Tree Implementation

This directory contains a complete, robust, and clean implementation of a standard **B-Tree** in C++.

A **B-Tree** is a self-balancing search tree designed to store sorted data and allow search, sequential access, insertion, and deletion in logarithmic time. Unlike a B+ Tree, keys and data/pointers are stored in **both** internal and leaf nodes.

---

## 🌟 Key Features

1. **Customizable Minimum Degree ($t$):** Fully customizable minimum degree through the constructor (defaults to $t = 2$, which defines a 2-3-4 tree).
2. **Proactive Top-Down Splitting:** Performs proactive splitting of full nodes as it traverses down the tree during insertion. This avoids recursive parent-backtracking and is extremely stable and clean.
3. **No Leaf-Linkage (B-Tree Property):** Leaf nodes are not linked, demonstrating standard B-Tree properties where elements must be searched hierarchically.
4. **Beautiful Visual Printout:** A visual, hierarchical tree display showing exactly where keys are positioned and how parent-child relationships are laid out.
5. **Interactive Console Menu:** Allows real-time user testing, key insertions, searches, and visualizations in the terminal.

---

## 📁 File Structure

- [bplus_tree.cpp](file:///Users/tanu/code/scaler-Adv-DBMS/lab6/bplus_tree.cpp): Main implementation file.
- `bplus_tree`: Precompiled executable binary.

---

## 🛠️ Compilation & Execution

To compile the B-Tree source code manually, run the following command in your terminal from this directory:

```bash
g++ -std=c++17 bplus_tree.cpp -o bplus_tree
```

To run the compiled binary:

```bash
./bplus_tree
```

---

## 🖥️ How it Works & Visual Output

When you run the program, it first runs a **step-by-step automatic demonstration** inserting keys `10, 20, 30, 40, 50, 60, 70, 80` into a B-Tree of **Degree 2** ($t = 2$, meaning max 3 keys per node and max 4 children).

### Automatic Demonstration Output Example:

```text
=========================================
       B-Tree Visualizer & Lab Demo      
=========================================
Creating a B-Tree of Degree t = 2 (Max keys per node = 3)

--- Step 1: Automatic Demo (Inserting 8 keys) ---

Inserting key: 10...
├─ [ 10 ]

Inserting key: 20...
├─ [ 10, 20 ]

Inserting key: 30...
├─ [ 10, 20, 30 ]

Inserting key: 40...
├─ [ 20 ]
    ├─ [ 10 ]
    ├─ [ 30, 40 ]

Inserting key: 50...
├─ [ 20 ]
    ├─ [ 10 ]
    ├─ [ 30, 40, 50 ]

Inserting key: 60...
├─ [ 20, 40 ]
    ├─ [ 10 ]
    ├─ [ 30 ]
    ├─ [ 50, 60 ]
...
```
