# Lab 5 — Red-Black Tree

> **Course:** Advanced DBMS
> **Author:** Bhavya Jain
> **Roll No:** 23BCS10088
> **Email:** Bhavya.23bcs10088@sst.scaler.com
> **Language:** C++17

A self-contained Red-Black Tree implementation following CLRS, with a single
shared `nil` sentinel and the classic insert/delete fixup cases. Used as the
in-memory index primitive that motivates database B-Tree variants in later
labs.

## Files

| File | Purpose |
| --- | --- |
| `RedBlackTree.h`   | Public API and node/color enums |
| `RedBlackTree.cpp` | Insert, erase, rotations, fixups, validation |
| `main.cpp`         | Driver: directed tests + 10k-op randomized stress |
| `Makefile`         | `make`, `make run`, `make clean` |

## Build & Run

```bash
make run
```

Expected: directed tests print the tree level-order, then a stress test runs
10 000 random insert/find/erase operations and verifies the tree against a
mirror `std::vector`. Output ends with `All RB-Tree tests passed.`

## Public API

```cpp
class RedBlackTree {
public:
    bool   find(int key) const;
    void   insert(int key);
    bool   erase(int key);     // returns false if key absent
    size_t size() const;
    bool   empty() const;

    void              print()    const;  // BFS, LeetCode-style "[a, b, null, c]"
    std::vector<int>  inorder()  const;  // sorted output
    bool              validate() const;  // checks all 5 RB invariants
};
```

## Invariants enforced

1. Every node is `RED` or `BLACK`.
2. The root is `BLACK`.
3. Every leaf (the shared sentinel) is `BLACK`.
4. A `RED` node has only `BLACK` children (no two consecutive reds).
5. Every root-to-leaf path crosses the same number of `BLACK` nodes.

`validate()` checks (2), (4), (5) on demand; the stress test calls it every
256 ops.

## Implementation notes

- **Single sentinel** for `nil`: simplifies rotations, removes most null
  checks, and lets `eraseFixup` walk `x->parent` even when `x == nil`.
- **Insert** is the textbook three-case fixup (recolor / rotate-then-recolor)
  plus mirror for the right-child branch.
- **Erase** uses `transplant` and the four-case `eraseFixup` from CLRS,
  including the subtle `x->parent = y` assignment when the successor is the
  removed node's direct child.
- **Strong RAII**: destructor walks the tree; no smart pointers needed since
  ownership is unambiguous.

## Why a Red-Black Tree in a DBMS course?

In-memory index structures (the engine's transient catalog, MVCC version
chains, lock managers) use balanced BSTs precisely because every operation is
`O(log n)` worst-case. RB-Trees give *worst-case* guarantees with cheap
rotations — the same family of trade-offs that motivates B-Trees and
B+-Trees on disk in the next lab.
