# Lab 4: Red-Black Tree & B-Tree in C++

## What this lab covers

Two index data structures used in databases:
- **Red-Black Tree** — in-memory self-balancing BST (used in `std::map`, in-memory indexes)
- **B-Tree (order t=2)** — disk-oriented multi-key tree (used by PostgreSQL, MySQL, SQLite indexes)

## Build & Run

```bash
# Red-Black Tree
g++ -std=c++17 -o rbt rbt.cpp && ./rbt

# B-Tree
g++ -std=c++17 -o btree btree.cpp && ./btree
```

## Red-Black Tree output

```
Inorder (key + color R/B):
1B 5R 10B 15R 20B 25R 30B
After removing 20:
1B 5R 10B 15R 25B 30R
```

## B-Tree output

```
Inorder after inserts:
1 3 5 6 7 10 12 17 20 25 30
Search 17: found
Search 99: not found
Inorder after removing 6 and 20:
1 3 5 7 10 12 17 25 30
```

## When to use which

| Property         | Red-Black Tree              | B-Tree (order t)                   |
|------------------|-----------------------------|------------------------------------|
| Storage target   | In-memory                   | Disk (one node = one page)         |
| Height           | O(log n)                    | O(log_t n) — shorter for large t   |
| Cache friendly   | Poor (pointer chasing)      | Excellent (sequential keys/node)   |
| Use in databases | In-memory indexes, std::map | On-disk indexes (PG, MySQL, SQLite)|

PostgreSQL's B-Tree index pages are 8 KB — matching the buffer pool page size so one disk read fetches an entire B-Tree node with potentially hundreds of keys.
