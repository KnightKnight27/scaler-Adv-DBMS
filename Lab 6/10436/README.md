# Lab 6 — Full B+Tree in C++

**Student:** Romit Raj Sahu | **Roll:** 24BCS10436

---

## 1. What is a B+Tree and why do databases use it?

A B+Tree is a self-balancing multi-way search tree where all values are stored in **leaf nodes**, and internal nodes hold only keys used for routing. Databases (MySQL InnoDB, PostgreSQL, SQLite) use B+Trees as their primary index structure because:

- **Guaranteed O(log n) search, insert, and delete** — the tree stays balanced after every operation
- **High fanout** — a single node holds many keys, so a tree over millions of records is only 3–4 levels deep
- **Sequential scans are efficient** — the leaf linked list enables range queries without traversing back up the tree

---

## 2. B+Tree vs B-Tree

| Property | B-Tree | B+Tree |
|----------|--------|--------|
| Values stored | At every node | Leaves only |
| Internal nodes | Hold key + value | Hold key only (routing) |
| Leaf linked list | No | Yes — leaves chained with `next` pointers |
| Range scan | Requires in-order traversal (recursive) | Walk leaf chain: fast and cache-friendly |
| Space efficiency | Values at every level waste internal space | Internal nodes pack more keys → higher fanout |

---

## 3. Node Structure

**Internal node:**
```
keys:     [k1 | k2 | k3]
children: [c0 | c1 | c2 | c3]
```
`c0` holds all keys < `k1`; `c1` holds keys in `[k1, k2)`; and so on.

**Leaf node:**
```
keys:   [k1 | k2 | k3]
values: [v1 | v2 | v3]
next -> (next leaf node)
```
Leaf nodes form a **doubly-linked singly-linked list** ordered by key — enabling range scans.

This implementation uses `MAX_KEYS = 4`: each node holds at most 4 keys. A node with 5 keys triggers a split. Minimum fill for any non-root node: `ceil(4 / 2) = 2` keys.

---

## 4. Insert with Split — Worked Example

Suppose a leaf currently holds `[5 | 8 | 11 | 14]` (full at MAX_KEYS=4).

Insert key `10`:
1. Leaf becomes `[5 | 8 | 10 | 11 | 14]` — 5 keys, overflow
2. Split at index `ceil(5/2) = 3`: left = `[5 | 8 | 10]`, right = `[11 | 14]`
3. Link right leaf into the chain: `left->next = right`
4. Push first key of right (`11`) up to the parent
5. If parent overflows, split internal node (middle key is pushed up, not kept)

---

## 5. Delete with Borrow — Redistribution Example

Tree state: leaf A = `[5 | 8]` (minimum), leaf B (right sibling) = `[11 | 14 | 17]`. Parent separator = `11`.

Delete `8` from A:
- A now has `[5]` — underflow (1 < min=2)
- Right sibling B has 3 keys > minimum → **borrow from right**
- Move B's first key (`11`) to end of A: A = `[5 | 11]`
- Update parent separator to B's new first key (`14`): B = `[14 | 17]`

No merge needed — A is back to minimum fill.

---

## 6. Delete with Merge — Example

Leaf A = `[5]` after deletion (underflow), left sibling = `[3]` (already at minimum), right sibling = `[11]` (at minimum). Neither sibling can lend.

**Merge A with right sibling** (or left):
1. Merge A and right: combined = `[5 | 11]`
2. Pull down parent separator that was between them — it is removed from parent
3. The merged node = `[5 | 11]`; parent loses one key and one child pointer
4. If parent now underflows, propagate `fix_underflow` up

---

## 7. Why the Leaf Linked List Makes Range Scans Efficient

**Without linked list (B-Tree style):**
To range-scan `[lo, hi]`, after finding `lo` you must traverse the tree back up and then down for each successive key: **O(k log n)** for k results.

**With leaf linked list (B+Tree):**
Find the leaf containing `lo` in O(log n). Then walk `leaf->next` pointers collecting all keys ≤ hi. Each next-pointer hop is O(1). Total: **O(log n + k)**.

For large result sets this is dramatically faster and also CPU-cache-friendly — leaf nodes are often stored in adjacent disk pages.

---

## 8. MAX_KEYS Choice and Minimum Fill Rule

`MAX_KEYS = 4` is chosen for this lab to make splits and merges visible with small datasets.

In a real database, `MAX_KEYS` is determined by the **page size** (typically 4KB or 16KB) and the size of each key + pointer. A 4KB page with 8-byte integer keys and 8-byte pointers can hold ~250 keys per internal node — far higher fanout means far fewer tree levels.

**Minimum fill rule:** a non-root node must hold at least `ceil(MAX_KEYS / 2)` keys. With `MAX_KEYS = 4`:
- Minimum = `ceil(4 / 2) = 2` keys
- This ensures the tree stays at least 50% full, bounding wasted space
- The root is exempt — it can have as few as 1 key (with 2 children)
