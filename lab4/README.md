# Lab Session 4: Red-Black Tree & Full B-Tree in C++

## Objective
Implement a Red-Black Tree (self-balancing BST used in database index structures) and a full B-Tree (the actual on-disk index structure used by PostgreSQL, MySQL, SQLite) supporting insert, merge (split promotion), and delete with underflow merging.

---

# Part 1: Red-Black Tree

## Properties
1. Every node is Red or Black.
2. Root is Black.
3. No two consecutive Red nodes (Red node's parent must be Black).
4. Every path from a node to its NULL descendants has the same number of Black nodes.

These invariants guarantee O(log n) height.

The source code is located in [rbt.cpp](file:///C:/Users/singh/Downloads/scaler-Adv-DBMS-main/scaler-adv-dmbs/lab4/rbt.cpp).

Compile and run:
```bash
g++ -std=c++17 -o rbt rbt.cpp
./rbt
```

---

# Part 2: Full B-Tree (order t)

Every internal node holds between `t-1` and `2t-1` keys, and has between `t` and `2t` children. A leaf holds `t-1` to `2t-1` keys. Root may hold as few as 1 key.

Supported operations: **insert** (split on the way down), **search**, **delete** (merge / borrow on the way down).

The source code is located in [btree.cpp](file:///C:/Users/singh/Downloads/scaler-Adv-DBMS-main/scaler-adv-dmbs/lab4/btree.cpp).

### Code Correctness Fix
In the original lab instructions, `split_child` contained out-of-order execution that led to out-of-bounds array access (accessing index `T - 1` after resizing the vector to `T - 1` which leaves indices `0` to `T - 2`).
We corrected it in [btree.cpp](file:///C:/Users/singh/Downloads/scaler-Adv-DBMS-main/scaler-adv-dmbs/lab4/btree.cpp#L16-L32) to capture the median key `med` before resizing:
```cpp
    void split_child(BNode* parent, int i) {
        BNode* y = parent->children[i];
        BNode* z = new BNode();
        z->leaf  = y->leaf;

        // z gets the right half of y's keys
        z->keys.assign(y->keys.begin() + T, y->keys.end());
        int med = y->keys[T - 1]; // Save median before resize
        y->keys.resize(T - 1);    // Resize safely

        if (!y->leaf) {
            z->children.assign(y->children.begin() + T, y->children.end());
            y->children.resize(T);
        }

        parent->keys.insert(parent->keys.begin() + i, med);
        parent->children.insert(parent->children.begin() + i + 1, z);
    }
```

Compile and run:
```bash
g++ -std=c++17 -o btree btree.cpp
./btree
```

---

## Red-Black Tree vs B-Tree — When to use which

| Property           | Red-Black Tree                        | B-Tree (order t)                          |
|--------------------|---------------------------------------|-------------------------------------------|
| Storage            | In-memory                             | Designed for disk (large node = 1 page)   |
| Node size          | 1 key per node                        | Up to `2t-1` keys per node                |
| Height             | O(log n)                              | O(log_t n) — much shorter for large t     |
| Use in databases   | In-memory indexes, std::map           | On-disk indexes (PostgreSQL, MySQL, InnoDB)|
| Cache friendliness | Poor (pointer chasing)                | Excellent (sequential keys in one node)   |
| Merge/split        | Rotation only                         | Child split / sibling borrow / merge      |

PostgreSQL's B-Tree index pages are 8 KB by default — matching `page_size` from Lab 2 — so one disk read fetches an entire B-Tree node.

---

## Key Takeaways
- Red-Black Trees maintain balance via color + rotation; ideal for in-memory sorted maps.
- B-Trees minimize disk I/O by packing many keys into one node = one page read.
- Delete in B-Trees has three cases: borrow from sibling, merge with sibling, or replace with predecessor/successor.
- The `T` (minimum degree) directly controls the fanout and tree height — higher T → shorter tree → fewer disk seeks.
