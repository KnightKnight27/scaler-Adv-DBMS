# Lab 4 — Red-Black Tree & B-Tree

**Student:** Indrajeet Yadav | **Roll No:** 23BCS10199

---

## Objective

Implement two foundational index data structures used in real databases:
1. **Red-Black Tree** — the in-memory self-balancing BST behind `std::map`, `std::set`, and in-memory database indexes.
2. **Full B-Tree** — the on-disk index structure used by PostgreSQL, MySQL (InnoDB), SQLite, Oracle, and virtually every production relational database.

---

## Build & Run

```bash
# Red-Black Tree
g++ -std=c++17 -Wall -Wextra -O2 rbt.cpp -o rbt && ./rbt

# B-Tree
g++ -std=c++17 -Wall -Wextra -O2 btree.cpp -o btree && ./btree
```

---

## Part 1: Red-Black Tree

### The Four Invariants

A Red-Black Tree is a BST with additional color constraints that guarantee O(log n) height:

| Property | Rule |
|----------|------|
| 1 | Every node is either RED or BLACK. |
| 2 | The root is always BLACK. |
| 3 | No two consecutive RED nodes (a RED node's parent must be BLACK). |
| 4 | Every path from any node to any descendant NIL leaf has the **same number of BLACK nodes** (black-height). |

These four properties together imply: **no path is more than twice as long as any other path** (the longest possible path alternates red and black; the shortest is all black). Therefore height ≤ 2 × log₂(n+1).

### Why a Sentinel NIL Node?

Instead of using `nullptr` for "no child", this implementation uses a single shared sentinel `nil_` node (colored BLACK). This simplifies the rotation and fix-up code: every leaf's children point to `nil_`, and `nil_`'s parent, left, right all point to `nil_`. No null-pointer checks needed in the core logic.

### Insert: Three Cases

After a standard BST insert (new node z is RED), Property 3 may be violated if z's parent is also RED.

```
Case 1: Uncle is RED
──────────────────
       Gp(B)                  Gp(R) ← push RED up
      /     \       →        /     \
   P(R)    U(R)           P(B)    U(B)
   /                       /
  z(R)                   z(R)

Then continue fix-up at Gp.

Case 2: Uncle is BLACK, z is "triangle" (parent-grandparent-z form a zig-zag)
──────────────────────────────────────────────────────────────────────────────
       Gp(B)                  Gp(B)
      /     \       →        /     \
   P(R)    U(B)           z(R)    U(B)
      \                   /
      z(R)             P(R)

Rotate P in the direction away from z. Now z is a "line". Fall through to Case 3.

Case 3: Uncle is BLACK, z is "line" (z, P, Gp are collinear)
──────────────────────────────────────────────────────────────
       Gp(B)                  P(B)
      /     \       →        /     \
   P(R)    U(B)           z(R)    Gp(R)
   /                                \
  z(R)                              U(B)

Recolor P→BLACK, Gp→RED. Rotate Gp away from z. Done.
```

### Delete: Structure + Fix-Up

Deletion is the hardest operation in an RB tree. Two phases:

**Phase 1 (structural):** Remove the node from the BST. Three sub-cases:
- Node has no left child → replace with right child.
- Node has no right child → replace with left child.
- Node has two children → find in-order successor (minimum of right subtree), copy its key into the node, delete the successor (which has at most one child).

**Phase 2 (fix_delete):** If the removed node was BLACK, we have a "double-black" deficit on one path. Resolved by 4 cases (plus mirrors):

| Case | Condition | Action |
|------|-----------|--------|
| 1 | Sibling is RED | Rotate parent toward x; recolor. Converts to Case 2/3/4. |
| 2 | Sibling is BLACK, both nephews BLACK | Color sibling RED; push double-black up to parent. |
| 3 | Sibling is BLACK, far nephew BLACK, near nephew RED | Rotate sibling; recolor. Converts to Case 4. |
| 4 | Sibling is BLACK, far nephew RED | Absorb double-black: rotate parent, recolor. Done. |

### Complexity

| Operation | Time | Space |
|-----------|------|-------|
| Insert | O(log n) | O(1) rotations |
| Delete | O(log n) | O(1) rotations |
| Search | O(log n) | O(1) |
| Space | — | O(n) |

---

## Part 2: B-Tree (Minimum Degree t)

### Why B-Trees Exist

A Red-Black Tree node holds one key and has two children. If we store 1 million keys, the tree has ~20 levels. Each node access in an on-disk index costs one disk I/O — so a lookup costs 20 disk reads, each taking 100 µs on NVMe → 2 ms. That's acceptable for a single lookup, but a range scan touching 1,000 records = 20,000 disk reads = 2 seconds.

A B-Tree solves this by packing **many keys into one node** (= one disk page). With t=100 and an 8 KiB page:

```
height = ceil(log_100(1,000,000)) = 3
Each lookup:  3 disk reads × 100 µs = 0.3 ms
Range scan:   traverse 1 page per 100 rows instead of 1 page per 1 row
```

### B-Tree Invariants

For a B-Tree of minimum degree t:

| Rule | Details |
|------|---------|
| Every non-root node has | at least t-1 keys |
| Every node has | at most 2t-1 keys |
| Every internal node with k keys has | exactly k+1 children |
| All leaves are at the same depth | (perfect balance) |
| Root has | at least 1 key (up to 2t-1) |

A node is **full** when it has exactly 2t-1 keys. Inserting into a full node requires a **split** — the median key is promoted to the parent.

### Insert: Proactive Split-on-the-Way-Down

This implementation uses the single-pass top-down approach:
- Before descending into any child, if that child is **full**, split it first.
- This guarantees: when we arrive at a leaf, there is always room to insert.
- No need to backtrack. Each node is visited at most twice.

```
Split of full child y (has 2t-1 keys):

Before:   parent: [...|...|...]
                       |
                       y: [k0|k1|...|k_{t-2}|MEDIAN|k_t|...|k_{2t-2}]

After:    parent: [...|...|MEDIAN|...|...]
                            /         \
                 y:[k0..k_{t-2}]   z:[k_t..k_{2t-2}]
```

The median key is promoted to the parent. y keeps the left half; z gets the right half. If y was an internal node, its children are split proportionally.

### Delete: Three Cases

Before descending into a child for deletion, ensure the child has ≥ T keys (otherwise fill it first).

| Case | Condition | Action |
|------|-----------|--------|
| 1 | Key is in a leaf | Remove directly. |
| 2a | Key is in an internal node, left child has ≥ T keys | Replace with in-order predecessor, delete predecessor from left subtree. |
| 2b | Key is in an internal node, right child has ≥ T keys | Replace with in-order successor, delete successor from right subtree. |
| 2c | Key is in an internal node, both children have T-1 keys | Merge: left child + separator key + right child → single node (2T-1 keys). Delete from merged node. |
| 3 (fill) | Key not in current node, target child has T-1 keys | Borrow from left sibling, OR borrow from right sibling, OR merge with a sibling. |

### Borrow vs Merge

**Borrow from left sibling:**
```
Before: parent: [...|sep|...]   After: parent: [...|sibling.last|...]
                    /                               /
     sibling:[a|b|c]  child:[x]    sibling:[a|b]  child:[sep|x]
```
The separator in parent descends into child; sibling's last key ascends to become the new separator.

**Merge with right sibling:**
```
Before: parent: [...|sep|...]     After: parent: [......]
                   /     \                         |
         left:[a|b]  right:[x|y]    merged:[a|b|sep|x|y]
```
The separator descends; left and right become one node. Parent loses one key and one child.

### B-Tree vs B+-Tree

In a **B-Tree** (this implementation), internal nodes hold real data values. In a **B+-Tree** (what PostgreSQL actually uses):
- Internal nodes hold only keys for routing — no data values.
- All data lives in leaf nodes.
- Leaf nodes are linked in a doubly-linked list for efficient range scans.

B+-Trees are preferred for databases because:
1. Internal nodes can hold more keys per page (no data payload) → shallower tree.
2. Range scans follow the leaf linked list → no backtracking through internal nodes.

### Real-World Database Connection

```
PostgreSQL index page (8 KiB):
  Header (24 bytes) + ItemIds + Items + Free space

  One B-tree index page holds approximately:
    8192 / (key_size + pointer_size)
  For a 4-byte integer key + 8-byte pointer = ~680 entries per page
  → t ≈ 340
  → A 1-billion-row table needs height = ceil(log_340(1e9)) = 2 levels

This is why PostgreSQL B-tree lookups on indexed columns are nearly instant:
  - Root page (almost always hot in shared buffer) → 1 memory access
  - 1-2 internal pages (usually cached) → 1-2 disk reads if cold
  - 1 leaf page → 1 disk read
  Total: 2-3 disk reads maximum for any indexed lookup on any size table.
```

---

## Red-Black Tree vs B-Tree: When to Use Which

| Property | Red-Black Tree | B-Tree (order t) |
|----------|---------------|-----------------|
| Storage target | Main memory (RAM) | Disk / SSD (page-aligned) |
| Node size | 1 key, 2 children | Up to 2t-1 keys, 2t children |
| Height | O(log₂ n) | O(log_t n) — much shorter for large t |
| I/O per operation | N/A (all in RAM) | O(log_t n) disk reads |
| Cache friendliness | Poor (pointer chasing through many nodes) | Excellent (sequential keys in one page) |
| Typical use | In-memory sorted maps (std::map, TreeMap, kernel rbtree) | On-disk indexes (PostgreSQL, MySQL, SQLite, Oracle) |
| Split / rebalance | Rotation (O(1) structural changes) | Child split + key promotion (O(t) work per split) |
| Merge on delete | Rotation + recolor | Child merge + key demotion |

---

## Key Takeaways

1. **Red-Black Trees maintain O(log n) height** through color invariants. The guarantee is that no path can be more than twice as long as any other — enforced by equal black-heights and no consecutive reds.

2. **B-Trees pack many keys per node** to minimize disk I/O. The minimum degree t controls the fanout; higher t → shorter tree → fewer page reads per operation.

3. **Delete in B-Trees requires filling deficient children** before descending — either by borrowing from a sibling (no structural change in parent) or merging with a sibling (parent loses one key and one child).

4. **The 'T' (minimum degree) directly maps to disk page size.** PostgreSQL uses 8 KiB pages; choosing t so that `2t * (key_size + ptr_size) ≤ 8192` maximizes fanout and minimizes tree height.

5. **Production databases use B+-Trees** (leaves linked in a list) rather than plain B-Trees, enabling both O(log_t n) point lookups AND O(k) range scans (k = number of results).
