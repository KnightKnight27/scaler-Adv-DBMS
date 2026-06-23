# Lab 4 — Red-Black Tree & B-Tree

## Concept

Databases need fast ordered indexes. Two self-balancing tree structures dominate:

- **Red-Black Tree** — in-memory index (e.g. `std::map`, MySQL memory engine). One key per node, O(log n) height via color constraints and rotations.
- **B-Tree** — on-disk index (PostgreSQL, MySQL InnoDB, SQLite). Many keys per node (one node = one disk page), O(log_t n) height where t is the minimum degree. Far fewer disk reads than RBT for the same number of keys.

## Part 1: Red-Black Tree

### Approach
Maintain 4 invariants:
1. Every node is Red or Black.
2. Root is always Black.
3. No two consecutive Red nodes.
4. Every root→NULL path has the same number of Black nodes.

On insert: new node starts Red → `fix_insert` walks up fixing violations via recolor (uncle is Red) or rotation (uncle is Black — triangle and line cases).

On delete: standard BST delete, then `fix_delete` restores the Black-height property if we removed a Black node — four cases involving the sibling's color and children.

### Key insight
Rotations are the mechanism; recoloring is the shortcut when both children of a grandparent are Red. The two cases (parent is left child vs right child) are perfect mirrors of each other.

### Result
```
Insert {10,20,30,15,25,5,1}:  1R 5B 10R 15B 20B 25R 30B
After remove 20:               1R 5B 10R 15B 25B 30B
After remove 10:               1B 5R 15B 25B 30B
```

---

## Part 2: B-Tree (minimum degree t=2)

### Approach
Each node holds `t-1` to `2t-1` keys. With t=2: 1–3 keys, 2–4 children.

**Insert** — proactively split full nodes (2t-1 keys) on the way down so there's always room to insert at a leaf without backtracking.

**Delete** — three cases when the key is in an internal node:
- Left child has ≥ t keys → replace with predecessor, delete predecessor
- Right child has ≥ t keys → replace with successor, delete successor
- Both have t-1 keys → merge children, delete from merged node

When descending to a child with only t-1 keys, first `fill` it — borrow from a sibling or merge — before recursing.

**Split** — median key of the child is promoted to the parent; left half stays, right half becomes a new sibling.

### Key insight
The proactive split-on-the-way-down means we never need to backtrack on insert. The fill-on-the-way-down mirrors this for delete. Height grows only when the root itself splits.

### Result
```
Insert {10,20,5,6,12,30,7,17,3,1,25}: 1 3 5 6 7 10 12 17 20 25 30
Search 17: found  |  Search 99: not found
After remove 6,20: 1 3 5 7 10 12 17 25 30
```

## RBT vs B-Tree

| | Red-Black Tree | B-Tree (t=2) |
|---|---|---|
| Storage target | RAM | Disk (1 node = 1 page) |
| Keys per node | 1 | 1 to 2t-1 |
| Height | O(log n) | O(log_t n) — much shorter |
| Used in | `std::map`, in-memory indexes | PostgreSQL/MySQL/SQLite indexes |
