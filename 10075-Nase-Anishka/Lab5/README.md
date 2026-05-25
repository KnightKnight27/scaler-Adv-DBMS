# LAB 5 — RED-BLACK TREE

> Roll Number: **10075** &nbsp;&nbsp;|&nbsp;&nbsp; Name: **Nase Anishka**
>
> A templated **Red-Black Tree** in C++17 with insert, lookup, ordered
> traversal, a tree-shape pretty printer, and a `validate()` function
> that *actually* checks all five RB invariants and throws if any of
> them is broken. Three demo scenarios exercise the tree on mixed,
> ascending, and descending inputs to show that rotations keep the
> height under control even on adversarial sequences.

---

# WHY THIS DATA STRUCTURE MATTERS FOR A DB COURSE

Red-Black trees are the in-memory ordered-set workhorse of basically
every database and language runtime I can think of:

* **Linux's Completely Fair Scheduler** keeps runnable tasks in an RB
  tree keyed by virtual runtime.
* **`std::map` / `std::set`** in libstdc++ and libc++ are RB trees.
* **Java's `TreeMap`** is an RB tree.
* PostgreSQL uses RB trees for **GiST/SP-GiST internal page sorting**,
  for **`pg_locks` bookkeeping**, and inside the planner's bitmap-AND
  paths.
* **`epoll`** in the Linux kernel uses an RB tree to keep track of the
  set of file descriptors a process is waiting on.

The reason is always the same — they give you `O(log n)` insert,
delete, and lookup with a much smaller constant factor than AVL, and
they don't suffer the catastrophic O(n) degradation of a plain BST on
sorted input.

---

# THE FIVE INVARIANTS

A Red-Black Tree is a BST where every node carries one extra bit (a
"color" — red or black) and the following five properties always hold:

1. Every node is **red or black**.
2. The **root is black**.
3. Every **NIL leaf** is treated as black.
4. **A red node cannot have a red child** (no two consecutive reds on
   any root-to-leaf path).
5. For every node, **all root-to-NIL paths through it pass through the
   same number of black nodes** (its "black-height").

Together (4) and (5) guarantee that the longest root-to-leaf path is
at most twice the shortest — i.e. the tree is height-balanced to
within a factor of 2, giving `O(log n)` operations.

My implementation enforces (1) and (3) by construction (the `Color`
enum has only two values; missing children are reported as black by
`colorOf`). Properties (2), (4), and (5) — plus the BST property —
are checked at runtime by `validate()`.

---

# FILES IN THIS FOLDER

* `main.cpp` — the `RedBlackTree<T>` template plus a driver that runs
  four scenarios end-to-end.
* `CMakeLists.txt` — C++17 with `-Wall -Wextra -Wpedantic`.
* `.gitignore` — build artefacts.
* `run_output.txt` — captured stdout from a real run, so the tree
  shapes are visible without rebuilding.
* `README.md` — this file.

---

# BUILD AND RUN

```bash
cd 10075-Nase-Anishka/Lab5
cmake -B build -S .
cmake --build build
./build/rb_tree
```

The driver prints, for each scenario:

* the input sequence
* `size`, `height`, `black-height`, in-order traversal
* a sideways ASCII rendering of the tree (right subtree on top, root
  in the middle, left subtree on the bottom — the standard `tree -R`
  style)
* the result of `validate()` — throws if any invariant is broken
* `contains(15)` / `contains(99)` for spot-checks

---

# SCENARIOS

## 1. Six mixed inserts: `10 20 30 15 5 1`

```text
        /----- 30(B)
20(B)
                /----- 15(B)
        /----- 10(R)
                /----- 5(B)
                        /----- 1(R)
```

* size 6, height 4, in-order `1 5 10 15 20 30` ✓
* Root is black (invariant 2) ✓
* No red-red parent-child pair (invariant 4) — e.g. 10 is red but its
  children 5 (B) and 15 (B) are both black; 1 is red but its parent 5
  is black. ✓
* Every root-to-NIL path has 2 black nodes excluding the root and
  including NIL — for example `20 → 30 → NIL` is `{30, NIL}` = 2
  blacks; `20 → 10 → 5 → 1 → NIL` is `{5, NIL}` = 2 blacks; `20 → 10 →
  15 → NIL` is `{15, NIL}` = 2 blacks (invariant 5). ✓

## 2. Ascending input: `1 2 3 4 5 6 7 8 9 10`

```text
                                /----- 10(R)
                        /----- 9(B)
                /----- 8(R)
                        /----- 7(B)
        /----- 6(B)
                /----- 5(B)
4(B)
                /----- 3(B)
        /----- 2(B)
                /----- 1(B)
```

* size 10, height 5. For a plain BST this same input would produce a
  fully right-leaning chain of height 10. The RB rotations cut the
  height to **5 = ceil(2 × log₂(10))** — exactly the theoretical bound.
* Root is `4`, which sits roughly in the middle of `1..10`.
* `validate()` passes.

## 3. Descending input: `10 9 8 7 6 5 4 3 2 1`

```text
                /----- 10(B)
        /----- 9(B)
                /----- 8(B)
7(B)
                /----- 6(B)
        /----- 5(B)
                        /----- 4(B)
                /----- 3(R)
                        /----- 2(B)
                                /----- 1(R)
```

* Same `size = 10` and `height = 5` — the mirror case of scenario 2
  is balanced equally well.
* `validate()` passes.

## 4. Duplicate-insert is a no-op

`insert(42)` called three times → `size() == 1`. My implementation
treats RB tree as a set of unique keys; a duplicate insert silently
returns. (If I were building a multimap, the right call would be to
allow duplicates and let them go right-of-equal, with care to track
identity in delete operations — but for this lab, plain set semantics
is enough.)

---

# IMPLEMENTATION NOTES

## Insert fixup in three cases

The classic CLRS fixup has six cases — three per side (parent is left
of grandparent vs. right). I collapsed those to **three** by computing
a `parentIsLeft` flag once and using it to pick the rotation direction
and the uncle. That makes the code half the length without losing any
clarity.

* **Case 1 — uncle is red:** recolor parent + uncle to black, recolor
  grandparent to red, and walk up two levels (continue the fixup from
  the grandparent). The black-height invariant is preserved because we
  swapped colors at one level, not added a black.
* **Case 2 — node is "inner" (LR or RL zigzag):** rotate at the parent
  so the node becomes an "outer" child of the grandparent, then fall
  through to Case 3.
* **Case 3 — node is "outer":** recolor parent to black + grandparent
  to red, then a single rotation at the grandparent.

In every case the loop either terminates immediately (Case 3) or
walks strictly upwards (Case 1), so insert fixup is `O(log n)` with
at most **two rotations** per insertion.

## `colorOf(nullptr) == Black`

To avoid scattering null checks everywhere, `colorOf` returns
`Color::Black` for `nullptr`. That's just invariant 3 in code form —
NIL leaves are black. It collapses every "if the uncle is null, treat
it as black" special case into a normal comparison.

## `validate()` checks all five invariants

Most "RB tree" code online stops at *building* the tree. I wanted a
function that would actually *catch* a bug if my rotations were
broken, so `validate()`:

1. Throws if the root is red (invariant 2).
2. Walks the tree post-order. At each node it asserts:
   * BST property: `left < node < right`.
   * Parent pointers are consistent (`left->parent == node`).
   * No red-red edge (invariant 4).
   * Both subtrees have the same black-height (invariant 5) — this is
     the one that would silently let an insert bug ship if I didn't
     check it.
3. Returns the subtree's black-height so the recursion can compare
   left and right children.

If you ever modify `rebalanceAfterInsert`, run the demo and watch
`validate()` either pass or throw a `std::logic_error` with a message
naming the broken invariant.

---

# FINAL THINGS I LEARNED

* RB trees are AVL trees' less-strict, more practical cousin: AVL is
  always height-balanced to within 1, RB only to within a factor of
  2. That looseness is exactly what lets RB trees do **fewer rotations
  on insert** (≤ 2 vs. AVL's O(log n) in the worst case) — which is
  why every general-purpose ordered-map ships RB, not AVL.
* The "color" is really a one-bit balance hint. The actual invariant
  that keeps the tree shallow is **invariant 5** (equal black-heights
  on every path); colors exist just to make that invariant cheap to
  maintain.
* Doing the fixup correctly means **never** thinking about red and
  black in isolation — every rebalancing move trades colors and
  rotations at the same time to keep the black-height equation true.
* Ascending and descending inputs — the two worst cases for a plain
  BST — produce identical heights in the RB tree. That's the
  practical demonstration of "self-balancing": the structure is the
  same up to mirroring regardless of input order.
* I implemented only **insert**. Real RB trees also need a delete
  operation, and delete is genuinely harder than insert because the
  rebalancing has to handle "double black" nodes. CLRS chapter 13.4
  covers it; I'd add that next.
* Writing a `validate()` function while writing the tree was the
  single most useful thing I did in this lab. It caught two bugs in
  my first draft — one in Case 2 (I rotated the wrong node) and one
  in Case 1 (I forgot to update the walking pointer to the
  grandparent). Without `validate()` I'd have shipped a tree that
  silently violated invariant 5 on certain inputs.
