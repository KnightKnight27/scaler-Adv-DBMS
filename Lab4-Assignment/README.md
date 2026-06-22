# Lab Session 4 — Solution: Red-Black Tree & Full B-Tree (C++)

My completed solution to **`lab_sessions/lab_4.txt`** — a Red-Black Tree and a
full B-Tree (insert with split-promotion, search, delete with borrow/merge),
both compiled, run, and **self-verified**.

## Files

| File | Purpose |
|------|---------|
| `rbt.cpp` | Red-Black Tree — insert, delete, + an invariant self-checker |
| `btree.cpp` | B-Tree of minimum degree `T` — insert/search/delete + structure printer |
| `rbt_output.txt`, `btree_output.txt` | Captured program output |
| `.gitignore` | Excludes the compiled binaries |

```bash
g++ -std=c++17 -O2 -o rbt   rbt.cpp   && ./rbt
g++ -std=c++17 -O2 -o btree btree.cpp && ./btree
```

---

## Bug I had to fix in the handout's B-Tree

The lab's `split_child()` shrank `y` and *then* read from the shrunk vector:

```cpp
y->keys.resize(T - 1);                       // y now has only T-1 keys
...
z->keys.assign(y->keys.begin() + T, ...);    // begin()+T is PAST end  -> UB
int med = y->keys[T - 1];                     // index T-1 is out of range -> UB
```

With `T=2`, after `resize(1)` the vector has a single element, so both
`begin()+2` and `keys[1]` are **out-of-bounds reads (undefined behaviour)**. The
correct order is to **capture the median and the right-half slice first, then
shrink `y`**:

```cpp
int med = y->keys[T - 1];                          // (1) capture median
z->keys.assign(y->keys.begin() + T, y->keys.end()); // (2) right half
y->keys.resize(T - 1);                              // (3) shrink y last
parent->keys.insert(parent->keys.begin() + i, med); // (4) promote
```

That single reordering is the difference between a B-Tree that silently
corrupts and one whose inorder traversal is provably sorted (shown below).

---

## Part 1 — Red-Black Tree

A binary search tree kept balanced by node *colors* + *rotations*. Four
invariants force the longest root→leaf path to be at most twice the shortest, so
height stays `O(log n)`:

1. every node is red or black; 2. the root is black; 3. a red node has no red
child; 4. every root→NULL path crosses the same number of black nodes.

My `rbt.cpp` adds a `check()` routine that recomputes the black-height and
rejects any red-red violation, so the program **proves its own correctness**
after each operation.

**Real output:**
```
Insert sequence: 10 20 30 15 25 5 1
Inorder (key + color R/B): 1R 5B 10R 15B 20B 25R 30B
Root is black: yes
Invariants hold: yes

After removing 20:        1R 5B 10R 15B 25B 30B
Invariants hold: yes
```

The inorder walk is sorted (BST property), the root is black, and the
black-height/no-red-red checks pass both after the 7 inserts and after deleting
the black internal node `20` — the case that triggers `fix_delete` rebalancing.

---

## Part 2 — Full B-Tree (minimum degree `T`)

A B-Tree generalises the BST to **many keys per node** so that one node maps to
one disk page. With `T=2` every non-root node holds 1–3 keys and 2–4 children.

- **Insert** splits any full (2T−1) child on the way down, promoting its median
  up — so the tree grows at the *root*, keeping all leaves at the same depth.
- **Delete** guarantees each node it descends into has ≥ T keys by *borrowing*
  from a sibling or *merging* with one; deleting an internal key replaces it with
  its in-order predecessor/successor.

**Real output:**
```
Insert sequence: 10 20 5 6 12 30 7 17 3 1 25

Inorder after inserts (must be sorted): 1 3 5 6 7 10 12 17 20 25 30
Tree structure (T=2):
[10]
    [6]
        [1 3 5] (leaf)
        [7] (leaf)
    [20]
        [12 17] (leaf)
        [25 30] (leaf)

Search 17: found
Search 99: not found

Remove 6 and 20:
Inorder after deletes: 1 3 5 7 10 12 17 25 30
Tree structure (T=2):
[5 10 17]
    [1 3] (leaf)
    [7] (leaf)
    [12] (leaf)
    [25 30] (leaf)
```

Things to notice:

- **All leaves are at the same depth** — the defining B-Tree property — both
  before and after deletion.
- Inserting 11 keys built a height-2 tree; the median-promotion (e.g. `10`, then
  `6`/`20`) is exactly the fixed `split_child` at work.
- Deleting `6` and `20` (internal-ish keys) triggered **merges**, which collapsed
  the height back to 1 with a fuller root `[5 10 17]`. The inorder output stays
  perfectly sorted — strong evidence the borrow/merge logic is correct.

---

## Red-Black Tree vs B-Tree — when to use which

| Property | Red-Black Tree | B-Tree (degree `T`) |
|----------|----------------|---------------------|
| Where it lives | In memory | On disk (one node = one page) |
| Keys per node | 1 | up to `2T−1` |
| Height | `O(log₂ n)` | `O(log_T n)` — much shorter for large `T` |
| Cache / I/O behaviour | Pointer-chasing, poor locality | Many keys per node → 1 read fetches a whole node |
| Rebalancing | Rotations + recolor | Split / borrow / merge |
| Used by | `std::map`, in-memory indexes, Linux CFS | PostgreSQL, MySQL/InnoDB, SQLite on-disk indexes |

The crux is the **height formula**. For a disk index, each level is potentially
one seek. A red-black tree of a million keys is ~40 levels deep (≈40 seeks); a
B-Tree with a fanout of, say, 100 is only `log₁₀₀(10⁶) ≈ 3` levels deep. That is
why every production on-disk index is a B-Tree (or B⁺-Tree), not an RB-tree —
**fanout buys shallowness, and shallowness buys fewer disk reads.**

PostgreSQL's default B-Tree index page is 8 KB — the same `page_size` seen in
Lab 2 — so one page read pulls in an entire node's worth of keys.

---

## Design trade-offs

- **`T` (fanout) controls everything.** Larger `T` → shorter tree → fewer disk
  seeks, but bigger nodes to read/write and shift on insert/delete. On-disk
  systems pick `T` so a node ≈ one page.
- **RB-tree wins in memory:** a single key per node and pure rotations are cheap
  when "I/O" is a cache line, not a disk seek; that's why `std::map` uses it.
- **B-Tree delete is genuinely harder** — three cases (borrow-left, borrow-right,
  merge) versus the RB-tree's rotation-only fix — which is exactly where the
  handout's off-by-one corruption lived until fixed.

---

## Key learnings

- Red-black balance is maintained by **color + rotation**; the invariant checker
  makes correctness observable rather than assumed.
- B-Trees minimise disk I/O by **packing many keys per node** so one page read =
  one node; the height advantage (`log_T n`) is the whole reason databases use
  them on disk.
- B-Tree delete has three sub-cases (borrow from sibling, merge, replace with
  predecessor/successor) and must **rebalance on the way down**.
- Order of operations matters: the `split_child` median must be captured **before**
  the node is resized — a textbook example of how a one-line reordering separates
  correct code from undefined behaviour.

---

### Reference

- Solution to `lab_sessions/lab_4.txt` (Advanced DBMS lab series).
- B-Tree algorithms follow CLRS (insert/split, delete/borrow/merge).
- Built with `g++` 13.3.0 `-std=c++17`, Ubuntu 24.04.
