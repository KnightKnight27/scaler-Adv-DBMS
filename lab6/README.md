# Lab 6: B-Tree Implementation

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
