# Lab 6 - B-Tree Index

## Student Details
- Name: Rudray Mehra
- Roll Number: 24BCS10425

## Objective
Implement a B-Tree and use it as a database index that maps a `Key` to a `Row`
payload. The tree must stay balanced after every insertion so that search,
insert, and ordered scan all run in O(log n).

## Files
- `btree_index.cpp`: Complete C++17 implementation of a templated `DB<Key, Row>`
  index backed by a B-Tree. Supports insertion with node splitting, point
  search, ordered scan (inorder traversal), and a structure printer.

## Compile and Run

```bash
g++ -std=c++17 LAB-6/btree_index.cpp -o LAB-6/btree_index
./LAB-6/btree_index
```

## B-Tree Properties Used
- Every node holds at most `2t - 1` entries and at most `2t` children, where `t`
  is the minimum degree (this build uses `t = 2`, i.e. a 2-3-4 tree).
- Every non-root node holds at least `t - 1` entries, so the tree height stays
  O(log n).
- Keys within a node are sorted; the `i`-th child subtree holds keys between
  separators `k[i-1]` and `k[i]` (multi-way binary-search ordering).
- All leaves are at the same depth — the tree grows only by splitting the root.

## How Insertion Stays Balanced
Insertion does a single top-down pass. Before descending into a child that is
already full (`2t - 1` entries), that child is split: its median entry is pushed
up into the parent and its remaining entries become two half-full nodes. Because
splitting happens on the way down, a parent is never full when its child splits,
so the split always has room. When the root itself is full it is split first and
a new root is created — this is the only step that increases tree height.

## Notes
The reference sketch in the source repository defined the `DB<Key, Row>` shape
and node layout but left `Insert`/`Search` incomplete. This lab completes that
design into a working, balanced index: search walks the separators in a node and
descends into the matching child range, exactly like a disk-backed index probe.
