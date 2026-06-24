# Red-Black Tree — C++ Implementation

A self-balancing Binary Search Tree that enforces the five Red-Black Tree
properties on every insertion and deletion. Includes an automated test suite
and an interactive CLI.

---

## Files

| File | Description |
|---|---|
| `rbt.hpp` | Full Red-Black Tree implementation (header-only) |
| `main.cpp` | Automated tests + interactive CLI |
| `Makefile` | Build, test, run, and clean targets |

---

## Build & Run

### Requirements
- `g++` with C++17 support (GCC 7+ or Clang 5+)
- `make`

### Commands

```bash
# Build
make

# Run interactive CLI (also runs tests first)
make run
# or
./rbt

# Run automated tests only
make test
# or
./rbt --run-tests

# Debug build (no optimisation, symbols for gdb)
make debug

# Clean binary
make clean
```

---

## Red-Black Tree Properties

The implementation enforces all five classic RBT rules:

| Rule | Description |
|---|---|
| 1 | Every node is either RED or BLACK |
| 2 | The root is always BLACK |
| 3 | Every leaf (nil sentinel) is BLACK |
| 4 | A RED node's children must both be BLACK |
| 5 | All paths from a node to descendant nil leaves have the same black-height |

---

## Automated Tests

Four tests run automatically on startup (or via `--run-tests`):

| Test | What it checks |
|---|---|
| Test 1 — Ascending inserts (1→20) | Right-leaning chain: rotations + recoloring |
| Test 2 — Descending inserts (20→1) | Left-leaning chain: mirror-case handling |
| Test 3 — Deletion cases | Leaf / single-child / two-children removal |
| Test 4 — Stress test (500 inserts, 300 deletes) | Random operations with fixed seed 42 |

Each operation calls `validateProperties()` internally via `assert`, so any
property violation causes an immediate failure with a descriptive message.

---

## CLI Commands

Start the interactive shell with `./rbt`:

```
rbt> insert 10
rbt> insert 20
rbt> insert 5
rbt> print
rbt> search 20
rbt> traverse
rbt> validate
rbt> delete 10
rbt> exit
```

| Command | Description |
|---|---|
| `insert <val>` | Insert an integer key |
| `delete <val>` | Remove a key (no-op if not found) |
| `search <val>` | Find a key and print its color |
| `print` | Display tree structure with R/B color tags |
| `traverse` | Print in-order, pre-order, post-order |
| `validate` | Verify all RBT properties programmatically |
| `help` | Show command list |
| `exit` | Quit the application |

---

## Implementation Notes

- **Nil sentinel** — a single shared `nil_` node (BLACK) is used for all leaf
  references, eliminating null-pointer checks throughout the code.
- **Duplicate keys** — silently ignored; each key appears at most once.
- **Destructor** — recursively frees all nodes; the sentinel is deleted
  separately after the tree is cleared.
- **`validateProperties()`** — checks root color, no consecutive red nodes,
  BST ordering, and equal black-height on every path in O(n) time.

---

## Complexity

| Operation | Average | Worst |
|---|---|---|
| Insert | O(log n) | O(log n) |
| Delete | O(log n) | O(log n) |
| Search | O(log n) | O(log n) |
| Validate | O(n) | O(n) |