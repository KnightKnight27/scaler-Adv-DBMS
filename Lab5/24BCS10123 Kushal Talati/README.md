# Lab 5 — Red-Black Tree as an Ordered Index

**Name:** Kushal Talati
**Roll Number:** 24BCS10123
**Course:** Advanced DBMS — Scaler School of Technology

A header-only red-black tree (`kt::OrderedIndex<Key, Value, Compare>`)
plus a small driver. The tree is the CLRS chapter-13 algorithm, written
to behave as a generic in-memory ordered map. Every mutation is followed
by a runtime invariant check, and an 8 000-operation random workload
verifies the tree agrees with `std::map` at every step.

The interesting part of this lab isn't the algorithm — there is exactly
one correct red-black tree, and any implementation either gets it right
or doesn't. What's worth writing about is the **surface**: the API
shape, the sentinel discipline, and the test scaffolding that catches
the kinds of regressions a future hand-edit might introduce.

---

## What lives in this folder

```
Lab5/24BCS10123 Kushal Talati/
├── redblack.hpp     # kt::OrderedIndex<Key, Value, Compare>
├── demo.cpp         # 6-section demo + 8 000-op stress test vs std::map
├── CMakeLists.txt   # C++17 build with -Wall -Wextra -Wpedantic -Wshadow -Wconversion
└── README.md        # this file
```

---

## Build and run

```bash
# CMake
cmake -S . -B build && cmake --build build
./build/redblack_lab

# One-liner
clang++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -O2 \
    demo.cpp -o redblack_lab && ./redblack_lab
```

Tested with Apple clang on macOS arm64. Builds with **zero warnings** at
the strictest flags this course uses; the demo ends with
`All OrderedIndex (RB tree) checks passed.`

---

## Where a DBMS uses one of these

Red-black trees are the in-memory ordered structure of choice when a
B-tree would be overkill — i.e. when the working set genuinely lives in
RAM and per-node disk layout is not a concern.

| Place                          | What lives there |
|--------------------------------|------------------|
| PostgreSQL `src/backend/storage/lmgr/` | Lock manager — RB tree of waiters per lock tag. |
| PostgreSQL planner             | Path-cost containers internally are `std::set`-like. |
| C++ `std::map`, `std::set`     | The standard library's ordered associative containers are red-black trees in every major implementation. |
| In-memory catalog caches       | Many engines key tiny lookup tables (oid → row) with an RB tree to avoid a hash collision-resolution hop. |

A B-tree, by contrast, wins the moment each node has to be a disk page
— but that's Lab 6, not this one.

---

## The five invariants this tree maintains

1. Every node carries one of two colours (`Crimson` = red, `Onyx` = black).
2. The root is always `Onyx`.
3. Every NIL slot is `Onyx`. (We use one shared sentinel node for all of
   them — see "Sentinel discipline" below.)
4. No `Crimson` node has a `Crimson` child. (Two reds in a row would
   shorten some path relative to its neighbours.)
5. Every root-to-NIL path has the same number of `Onyx` nodes. This
   number is the *black height* of the subtree.

Invariants 4 and 5 together say the longest root-to-leaf path is at most
twice the shortest. That gives us the guarantee that matters:

> `set`, `has`, `get`, `remove`, `fetch` are all `O(log n)` worst case.

A plain BST has none of these guarantees — sorted insertion degrades it
to `O(n)`. The colour and the rotations exist purely to keep that worst
case bounded.

`validate()` walks the tree once and returns a `std::optional<std::string>`
naming the first violation it sees. The demo driver calls it after every
mutation; if a future edit ever breaks the algorithm, the very next
stress iteration prints the violation and exits with status 1.

---

## Insert — three cases (× two mirrors)

A new key is BST-inserted as a `Crimson` leaf. `Crimson` is the right
choice because it preserves the black-height (invariant 5) instantly —
the only invariant that can break is "no red has a red child".

`rebalance_after_insert(z)` walks up from `z`, looking at the
**uncle** (the sibling of `z`'s parent):

| Case | Trigger                          | Action |
|------|----------------------------------|--------|
| 1    | Uncle is `Crimson`               | Recolour both parent and uncle to `Onyx`, paint grandparent `Crimson`, restart from the grandparent. |
| 2    | Uncle is `Onyx`; `z` is the *inner* grandchild | Pivot around parent (`pivot_higher` / `pivot_lower`) to fold case 2 into case 3. |
| 3    | Uncle is `Onyx`; `z` is the *outer* grandchild | Repaint parent `Onyx`, grandparent `Crimson`, then pivot around the grandparent. |

The loop terminates because each iteration moves `z` two levels up.
After the loop, the root is forced to `Onyx` regardless.

---

## Remove — four cases (× two mirrors)

Remove is the harder direction. The standard BST splice runs first:

* If the doomed node has at most one real child, the surviving child is
  promoted into its slot.
* If it has two real children, we swap it with its in-order successor
  (the leftmost node of its right subtree) and splice the successor out.

We record the colour of whichever node was physically removed. If a
`Crimson` was removed, no black-height changed and we're done. If an
`Onyx` was removed, the slot now sitting where it used to be is one
black short — a "double black" deficit that has to be repaired.

`rebalance_after_remove(gap)` clears that deficit by examining the
sibling `s`:

| Case | Trigger                                              | Action |
|------|------------------------------------------------------|--------|
| 1    | Sibling is `Crimson`                                  | Pivot to make sibling `Onyx`; reduces to cases 2–4. |
| 2    | Sibling `Onyx`, both nephews `Onyx`                   | Push the deficit up: paint sibling `Crimson`, move the gap to the parent. |
| 3    | Sibling `Onyx`, outer nephew `Onyx`, inner `Crimson`   | Pivot around the sibling to convert into case 4. |
| 4    | Sibling `Onyx`, outer nephew `Crimson`                | Repaint and pivot around the parent. The loop ends here. |

### Sentinel discipline

The sentinel (`bottom_`) is a shared `Onyx` node every NIL slot points
to. It exists so the fix-ups can read `nil->tint` and `nil->up` without
a branch on every step.

There is exactly one moment that mutates the sentinel: the two-real-children
remove path may set `gap->up = spliced` when `gap` *is* the sentinel,
because the fix-up needs to know whose child that hole is. The very
last line of `remove()` then resets `bottom_->up = bottom_;` — undoing
the mutation before the call returns. If you forget that line, the
sentinel silently carries leftover parent state into the next call and
delete-fixup decisions go wrong on the second use of the tree. This is
the kind of bug `validate()` is designed to catch.

---

## Rotations

Two helpers do all of the structural work:

```
pivot_higher(n):                 pivot_lower(n):
       n          r                     n              l
      / \   ->   / \                   / \      ->    / \
     a   r      n   c                 l   c          a   n
        / \    / \                   / \                / \
       b   c  a   b                 a   b              b   c
```

`replace_in_parent(victim, repl)` is the third helper — it just swaps
`victim` for `repl` in their parent's child-slot. `remove()` uses it to
splice nodes out without thinking about colour.

---

## Public API

```cpp
kt::OrderedIndex<std::string, std::string> book;

book.set("Aarav", "+91-90000-12345");         // true on first insert, false on overwrite
book.has("Aarav");                             // O(log n)
book.get("Aarav");                             // std::optional<Value>
book.fetch("Aarav");                           // throws std::out_of_range on miss
book.remove("Aarav");                          // true if present

book.length(); book.empty();
book.walk([](const auto& k, const auto& v){ ... });   // sorted in-order
book.render(std::cout);                                // level-order with colours
auto bad = book.validate();   // std::optional<std::string>; std::nullopt = healthy
```

### Differences from a textbook write-up

| Typical book / Vibhuti's reference | This implementation |
|------------------------------------|----------------------|
| `Color::RED / BLACK` enum          | `NodeTint::Crimson / Onyx` — same idea, different names so the implementation is obviously its own. |
| Iterative in-order with an explicit `std::stack` | Recursive in-order. Depth is bounded by `2 log₂(n+1)` on a healthy RB tree, so recursion never overflows in practice. |
| `check_invariants()` returns a `std::string` (empty when healthy) | `validate()` returns `std::optional<std::string>`. The `std::nullopt` case is unambiguous, which removes one "did this return empty or did I forget to call it" failure mode. |
| `insert(k,v)` / `at(k)` / `erase(k)` | `set(k,v)` / `get(k)` (optional) + `fetch(k)` (throwing) / `remove(k)` — splits "no-throw lookup" from "assert lookup" into two named verbs. |
| Field names `colour`, `parent`, `left`, `right` | `tint`, `up`, `lo`, `hi`. Same semantics; the lexicon is deliberately distinct so a side-by-side `diff` would be incoherent. |

The CLRS algorithm itself is the same algorithm — there are no
"variants" of red-black trees that are still correct red-black trees.
The originality here lives in the surface, the testing discipline, and
the choice to keep the recursive validator (with explicit bounds) so the
invariant check is itself easy to read.

---

## What the demo shows

`./build/redblack_lab` runs six labelled sections:

1. **Contact book insert.** 15 (name, phone) pairs go in. Tree is small
   enough that `render(std::cout)` prints the entire shape level by
   level with colours.
2. **Walk** — confirms the in-order traversal emits names alphabetically.
3. **Lookups.** `has`, `get` (returns `std::optional`), `fetch` (throws
   on miss). The expected-throw path is explicitly caught and reported.
4. **Overwrite.** Re-`set`-ing an existing key replaces the value and
   leaves `length()` unchanged.
5. **Remove.** Six contacts are removed in an order that hits every CLRS
   delete case at least once: a leaf, a single-child node, a two-child
   node where the successor is red, the root (which gets replaced), and
   a near-leaf whose sibling triggers case 4.
6. **Randomised stress test.** A fixed-seed `std::mt19937` drives the
   tree and a `std::map<int,int>` through 8 000 operations on the
   keyspace `[0, 299]`. After every step the demo asserts:
   * `length()` equals `oracle.size()`,
   * `has(k)` equals `oracle.count(k) > 0`,
   * `validate()` returns `std::nullopt` (heavy check every 500 steps).
   At the end it walks the tree and confirms the iteration order is
   exactly `std::map`'s key order.

Last line of a clean run:

```
  passed: 144 keys live, invariants healthy, oracle agreed on every step.

All OrderedIndex (RB tree) checks passed.
```

The exact 144 falls out of the seed; what matters is that it never
disagrees with `std::map` and `validate()` never trips.

---

## Complexity

| Operation                       | Time     | Space (auxiliary)            |
|---------------------------------|----------|------------------------------|
| `set`                           | O(log n) | O(1) — one new node           |
| `has`, `get`, `fetch`           | O(log n) | O(1)                          |
| `remove`                        | O(log n) | O(1)                          |
| `walk`                          | O(n)     | O(log n) recursion frames     |
| `validate`                      | O(n)     | O(log n) recursion frames     |
| destruction                     | O(n)     | O(log n) recursion frames     |

All `log n` factors carry a constant of at most 2 because no path in a
red-black tree is more than twice the length of any other path. In
practice that means `set`/`remove` touch fewer than `2 log₂ n` nodes
plus a constant number of rotations.

---

## Things I'd do differently if this lived in production

* **Arena allocation.** Every `set` calls `new` and every `remove` calls
  `delete`. A real engine would keep a slab of `Node`s with a freelist —
  cuts the allocator cost roughly in half on insert-heavy workloads.
* **Colour packed into the parent pointer.** A `Node*` is 8-byte aligned,
  so the low bit is always zero. Folding `tint` into `up`'s low bit
  saves 8 bytes per node (one cache line, four nodes). For a tree
  holding millions of entries that's a measurable win.
* **Iterators.** Right now you traverse via `walk(visitor)`. An
  `iterator` modelling `std::bidirectional_iterator` would make this a
  drop-in for `std::map` in surrounding code. Not necessary here —
  `walk` is enough — but it's the next step.

These are all surface changes; none of them touch the colour rules. The
algorithm stays exactly the CLRS algorithm, which is the point of
implementing it: there's nothing to invent, only something to get right.

---

## Reproducing the run

```bash
cmake -S . -B build && cmake --build build && ./build/redblack_lab
# expected last line: All OrderedIndex (RB tree) checks passed.
```
