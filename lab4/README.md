# Lab 4 — Balanced Search Structures: Red-Black Tree + B-Tree

This report covers the **B-Tree** half of Lab 4. The Red-Black Tree half is in the
companion file; both are balanced search structures, but they are tuned for very
different storage settings. The short version: a Red-Black tree is a balanced
*binary* tree built for fast in-memory operations, while a B-Tree is a balanced
*multi-way* tree built so that an index can live on disk and still answer queries
in only a handful of block reads. The reasoning behind that distinction is the
main point of this lab.

## 1. What a B-Tree is

A B-Tree of **minimum degree `t`** (also called the order) is a search tree in
which each node stores a *sorted array of keys* and, if internal, one more child
pointer than it has keys. A node with `c` keys has children that partition the
key space into `c+1` ranges:

```
            node: [ k0   k1   k2 ]
                  /    |    |     \
              < k0  k0..k1 k1..k2  > k2
```

The defining invariants, all maintained by every operation in `BTree.cpp`:

- Every node except the root holds between **`t-1` and `2t-1`** keys.
- The root holds between **1 and `2t-1`** keys (or the tree is empty).
- An internal node with `c` keys has exactly **`c+1` children**.
- Keys inside a node are sorted, and child `i` holds exactly the keys that fall
  between `key[i-1]` and `key[i]`.
- **All leaves are at the same depth.** The tree can only grow or shrink in
  height at the root, which is what keeps it perfectly height-balanced.

In the demo we use `t = 3`, so every node holds 2–5 keys (the root may hold 1–5).
Because every path from root to leaf has the same length, the height is
`O(log_t n)` — and with large `t` the base of that logarithm is large, so the
tree is extremely shallow.

## 2. Why B-Trees for database indexes (and why not RB-trees)

The whole design exists to minimise **disk seeks**. A disk (or even SSD) read is
thousands of times slower than a memory access, so the cost model of an on-disk
index is "number of nodes touched", not "number of comparisons". Three properties
of B-Trees attack that cost directly:

1. **High fan-out.** A node is sized to fill one disk block (say 4–16 KB). If a
   key+pointer pair is ~16 bytes, a single block holds *hundreds* of keys, so `t`
   can be in the hundreds. With fan-out 256, a tree of 16 million keys is only
   **3 levels deep** — three block reads to find any record.
2. **Block-aligned nodes.** Because one node = one block, every node read is a
   single I/O. We pay for the data we actually need, not for pointer-chasing
   across scattered memory.
3. **Fewer, larger reads.** A binary tree storing the same 16 million keys is
   ~24 levels deep. As an on-disk structure that is potentially 24 seeks per
   lookup — eight times worse than the B-Tree, and the gap widens with size.

A Red-Black tree is the right tool for the *opposite* environment. It is binary
(fan-out 2), so its nodes are tiny and live happily in RAM where random pointer
access is cheap. It guarantees `O(log n)` operations with only a constant number
of rotations per update, which makes it ideal for in-memory ordered maps and
sets. Put on disk, though, its depth and one-key-per-node layout would generate
far too many seeks. So the trade-off is clean:

| | Red-Black Tree | B-Tree |
|---|---|---|
| Branching | Binary (2) | Multi-way (up to `2t`) |
| Node size | Tiny (1 key) | Block-sized (many keys) |
| Height | `~2 log2 n` | `~log_t n` (very small) |
| Tuned for | In-memory access | On-disk / cache-line access |
| Rebalance | Rotations + recolour | Split / borrow / merge |

Both are balanced BSTs achieving logarithmic operations; they differ only in the
*width* they choose, and that width is dictated by where the data lives.

## 3. The algorithms

### Search
Identical idea to a BST, just wider. At each node scan the sorted keys for the
target; if found, return; if at a leaf, fail; otherwise descend into the one
child whose range contains the key. `O(log_t n)` nodes visited.

### Insert — split on the way down
Insertion always lands in a leaf. The only complication is keeping nodes within
`2t-1` keys. We use **proactive splitting**: as we walk down, any *full* child
(exactly `2t-1` keys) is split before we descend into it. Splitting a full node
sends its **median key up** into the parent and divides the rest into two nodes
of `t-1` keys each:

```
parent: [ .. M .. ]                 split child C around its median M
          /   \                     ->  parent gains M; C becomes two nodes
   C:[a b M c d]                          [a b]   [c d]
```

Because we only ever split a child whose parent is known to be non-full, the
median always has room in the parent. The single exception is a full *root*:
there we first make a new root above the old one and split into it. That is the
**only** way the tree gains height, which is exactly why all leaves stay level.

### Delete — borrow and merge
Deletion is the hard part. The strategy is to maintain a precondition: *before
descending into a child, make sure that child has at least `t` keys* (one more
than the minimum). Then any deletion in the subtree can safely remove a key
without underflowing. Several cases arise:

- **Key in a leaf:** erase it directly (the precondition guarantees the leaf can
  spare a key).
- **Key in an internal node:** we cannot just remove it — it is a separator. We
  replace it with its **predecessor** (rightmost key of the left subtree) if that
  subtree has `≥ t` keys, or its **successor** (leftmost key of the right
  subtree) if that one does, then recursively delete that predecessor/successor
  from the leaf where it actually lives. If *both* adjacent children are minimal,
  we **merge** them around the separator and delete from the merged node.
- **Restoring the precondition (`fill`):** when the child we want to enter has
  only `t-1` keys, we fix it first:
  - **Borrow from left sibling** (if it has `≥ t` keys): rotate a key right —
    the parent's separator drops into the child, and the left sibling's largest
    key rises to become the new separator.
    ```
    parent: [ .. S .. ]        parent: [ .. L .. ]
            /     \      ->            /     \
    sib:[..L]   child:[X]      sib:[..]   child:[S X]
    ```
  - **Borrow from right sibling** (symmetric): rotate a key left.
  - **Merge** (when neither sibling can spare a key): pull the separator down and
    fuse the child with a sibling into one node of `(t-1)+1+(t-1) = 2t-1` keys —
    exactly the maximum, so it stays valid.
    ```
    parent: [ A S B ]          parent: [ A B ]
              / \         ->            |
       [x]   [y]                     [x S y]
    ```
- **Shrinking the root:** a merge can leave the root with zero keys. When that
  happens its single remaining child becomes the new root and the tree loses a
  level. This is the mirror of root-splitting and the only way height decreases,
  again keeping all leaves at equal depth.

Every borrow and merge moves exactly one key through the parent, so the sorted
order and the child-count invariant are preserved at each step.

## 4. Sample output

Built with `t = 3` (nodes hold 2–5 keys). Each line is one level; `[ ]` is a
node. After inserting twenty keys:

```
L0: [10 14 20 40]
L1: [3 5 6 7 8] [12 13] [15 16 17] [25 30] [45 50 55 60]
In-order keys: 3 5 6 7 8 10 12 13 14 15 16 17 20 25 30 40 45 50 55 60
```

Deletions then exercise each hard case. `delete(13)` empties `[12 13]` toward the
minimum and triggers a **borrow**, reshaping the separators:

```
L0: [8 14 20 40]
L1: [3 5 7] [10 12] [15 16 17] [25 30] [45 50 55 60]
```

`delete(20)` removes an **internal** key, replaced by its neighbour. Then
deleting `3, 5, 7, 8, 10, 12` forces repeated **merges** and collapses the root
from four keys down to two:

```
L0: [17 40]
L1: [14 15 16] [25 30] [45 50 55 60]
In-order keys: 14 15 16 17 25 30 40 45 50 55 60
```

Deleting the rest empties the tree cleanly. Throughout, the in-order traversal
stays perfectly sorted, confirming the search-tree property is never violated.

## 5. Build and run

```
g++ -std=c++17 BTree.cpp -o btree
./btree
```

The `main()` demo inserts a sequence, prints the tree level-by-level, runs a few
searches, then performs deletes chosen to trigger a plain leaf delete, a borrow,
an internal-node delete, a cascade of merges, a root shrink, and finally a full
emptying — so each branch of the delete logic is visible in the output.

## 6. Connecting the two halves

The Red-Black tree and the B-Tree are two answers to the same question: *how do
you keep a search tree balanced so operations stay logarithmic?* The RB-tree
answers it for memory, using colour invariants and rotations to bound the height
of a binary tree. The B-Tree answers it for disk, widening each node to a full
block so the height — and therefore the number of expensive seeks — is tiny.
Studying them together makes the cost model explicit: the "right" balanced tree
is whichever one matches the speed gap between the levels of storage you are
actually using.
