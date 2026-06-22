# MiniDB

MiniDB is a small relational database we built from scratch for the Advanced DBMS capstone.
It isn't a wrapper around SQLite or anything like that — every layer is our own code: a
page-based storage engine, a B+ tree index, a tiny SQL engine with a cost-based optimizer,
transactions with crash recovery, and a concurrency engine that can run in either MVCC or
two-phase-locking mode. You talk to it through a small SQL prompt.

**Our extension track is B — Concurrency (MVCC).**

---

## Team

> **Team name:** `SmallDB-BigData`

| Full Name | Scaler Email | Roll Number |
| --------- | ------------ | ----------- |
| Krritin Keshan   | krritin.24bcs10122@sst.scaler.com    | 24BCS10122 |
| Abdullah Danish  | abdullah.24bcs10054@sst.scaler.com   | 24BCS10054 |
| Aniruddha Patil  | Aniruddha.23bcs10156@sst.scaler.com  | 23BCS10156 |

---

## 1. Project Overview

**The problem.** We wanted to actually understand how a database works inside, so we set out to
build one ourselves rather than read about one. The goal was to see how the classic pieces — disk
storage, indexing, query processing, concurrency, and recovery — fit together into a single
system that really runs.

**What we were aiming for.**

- Keep it small and correct. We implemented exactly what each component needs and nothing more, so
  every one of us can explain any line in the viva.
- Make it *one* system, not five disconnected lab programs glued together.
- Pick an extension that demonstrates a real trade-off you can measure, not just a checkbox.

**Why we chose Track B (MVCC).** The core project already requires two-phase locking. Track B asks
us to add MVCC *next to it* and compare the two — which is a genuinely interesting question:
under a read-heavy workload, 2PL readers have to wait for writers to release their locks, while
MVCC readers just read a consistent snapshot and never block. We thought that contrast would be
the most satisfying thing to build and the easiest to show off with a benchmark. (More in §9.)

---

## 2. System Architecture

A SQL string flows down through these layers and the answer comes back up:

```
          SQL text   e.g.  SELECT name FROM emp WHERE id = 3;
                              |
   ┌──────────────────────────▼───────────────────────────────┐
   │  QUERY LAYER                                               │
   │    Lexer  ->  Parser (recursive descent -> AST)           │
   │           ->  Optimizer  (which scan? which join order?)  │
   │           ->  Executor   (pull-based "Volcano" operators) │
   │              SeqScan · IndexScan · Filter · Project · Join │
   └─────────────┬──────────────────────────────┬──────────────┘
        reads     │                              │  writes (INSERT / DELETE)
                  ▼                              ▼
   ┌────────────────────────┐     ┌─────────────────────────────────┐
   │  INDEX                 │     │  TRANSACTIONS & RECOVERY          │
   │  B+ tree (key -> RowID)│     │  Database: BEGIN/COMMIT/ROLLBACK  │
   │  linked leaves = ranges│     │  WAL  (log first, then apply)     │
   └───────────┬────────────┘     │  MVCC + 2PL engine  (Track B)     │
               │ RowID            └────────────────┬──────────────────┘
               ▼                                   │ apply
   ┌───────────────────────────────────────────────▼───────────────────┐
   │  STORAGE ENGINE                                                     │
   │  Catalog -> HeapFile (slotted pages) -> BufferPool (clock-sweep)    │
   │                                       -> DiskManager -> .db file    │
   └────────────────────────────────────────────────────────────────────┘
```

**The modules** (everything lives under `src/`):

| Folder | What's in it | Its job |
|--------|--------------|---------|
| `common/` | types, value, config | the shared vocabulary: `PageID`, `RowID`, `Value` (an int or text), `PAGE_SIZE` |
| `storage/` | disk_manager, page, buffer_pool, heap_file | the durable, cached, slotted row store |
| `index/` | bplus_tree | primary-key index that maps a key to a `RowID`, with range scans |
| `catalog/` | schema, catalog, database | the table registry, row encoding, and the SQL session + transactions |
| `query/` | lexer, parser, ast, optimizer, executor | turns SQL into a plan and runs it |
| `txn/` | transaction, transaction_manager | the MVCC + 2PL concurrency engine (this is Track B) |
| `recovery/` | wal | the write-ahead log and crash recovery |

**How a request moves through it.** A SQL string is split into tokens, parsed into a small tree
(the AST), and turned into a plan made of operators. Reads travel down to the index/heap and pull
rows back up. Writes go through the `Database` session, which writes a log record *before*
touching the data, then applies the change and groups it into a transaction that either commits
for good or rolls back.

---

## 3. Storage Layer

This is the bottom of the stack — where rows actually live on disk.

- **Pages are 4 KB.** That's the natural unit: one page is roughly one disk read. The whole engine
  moves data around in pages.
- **Each page is a "slotted page."** At the front there's a small header and a directory of slots;
  at the back there are the actual tuple bytes. The directory grows forward, the tuples grow
  backward, and the free space is whatever's left in the middle. Each slot records where its tuple
  starts and how long it is (length `0` means "deleted"). The nice part: a row's address is
  `RowID = (page, slot)`, and that stays valid even if the bytes shuffle around inside the page —
  because the index points at the slot, not the raw offset.

  *Why slotted pages and not fixed-size records?* Our `TEXT` columns vary in length, so
  fixed-size slots would either waste space or not fit. Slotted pages handle variable-length rows
  and keep the `RowID` stable, which is exactly what the index needs.

- **The heap file** is just an unordered pile of these pages. Insert finds the first page with room
  (or makes a new one) and returns a `RowID`; get/erase work by `RowID`; scan walks every live row.
- **The buffer pool** keeps a fixed number of pages in memory so we're not hitting disk constantly.
  When it's full and needs room, it evicts a page using **clock-sweep**: each frame has a small
  "recently used" counter, and a hand sweeps around decrementing counters until it finds one at
  zero to evict. Pinned pages (in use right now) can't be evicted, and dirty (modified) pages are
  written back before they're reused.

  *Why clock-sweep instead of true LRU?* LRU needs you to reorder a linked list on every single
  access, which is fiddly. Clock-sweep approximates "least recently used" with one counter per
  frame and no list bookkeeping — it's O(1) and resists getting wiped out by a big scan. For a
  teaching database it's the sweet spot of simple and good-enough.

- **The disk manager** is the only piece that touches the file. It reads and writes whole pages by
  id (`offset = id × 4096`) and grows the file one page at a time. Keeping all file I/O in one
  place means the rest of the code never thinks about byte offsets.

We checked durability the honest way: write rows in one run of the program, quit, then read them
back in a fresh run.

---

## 4. Indexing

The index lets us find a row by its primary key without scanning the whole table.

- **It's a B+ tree** keyed on the integer primary key, and each key maps to a `RowID`.
- **Internal nodes only hold separator keys** that tell you which child to follow; **all the real
  `RowID`s live in the leaves**, and the leaves are linked left-to-right in a chain.
- **Searching** starts at the root and, at each internal node, walks right while the key is `≥` the
  separator, descends to the right child, and finally binary-searches the leaf. Inserting can
  overflow a node, in which case it splits and pushes a separator key up to the parent. A range
  scan finds the starting leaf and then just walks the leaf chain until it passes the high key.

  *Why a B+ tree and not a hash index or a plain B-tree?* A hash index is great for `id = 3` but
  can't do ranges like `id > 100` at all, and we wanted range scans. A plain B-tree stores data in
  *every* node, which lowers how many keys fit per node and makes range scans hop around the tree;
  a B+ tree keeps all data in the linked leaves, so ranges are just a walk along the bottom. That's
  why nearly every real database uses a B+ tree for this.

- **It's an in-memory index, rebuilt from the heap when a table opens.** It fully supports
  search/insert/delete and drives the index scans — we just don't serialize the tree nodes to
  their own disk pages. We made that call deliberately: persisting the tree to disk is a lot of
  extra page-format machinery for no new *concept*, and the heap is already the durable source of
  truth, so rebuilding the index on open is cheap and always correct.

---

## 5. Query Execution

This is how a SQL string becomes an answer.

- **Parsing.** We hand-wrote a recursive-descent parser. It walks the tokens and builds an AST for
  `CREATE`, `INSERT`, `SELECT`, `DELETE`, plus `BEGIN`/`COMMIT`/`ROLLBACK`. Operator precedence
  (`OR` is looser than `AND`, which is looser than a comparison) falls out naturally from how the
  parse functions call each other.

  *Why hand-write the parser instead of using a generator like yacc/bison?* A generator means an
  extra tool, a grammar file, and generated code none of us wrote — which is exactly what you don't
  want walking into a viva. A recursive-descent parser is just normal functions you can read top to
  bottom, and our SQL subset is small enough that it stays short.

- **Planning.** The planner turns the AST into a tree of operators, asking the optimizer which scan
  to use and how to order a join.
- **Execution — the Volcano (pull) model.** Every operator has `open()` and `next()`. A parent asks
  its child for one row at a time by calling `next()`, the child asks *its* child, and so on. We
  have `SeqScan`, `IndexScan`, `Filter`, `Project`, and `NestedLoopJoin`, and because they all
  share the same interface they snap together in any combination.

  *Why the pull/iterator model and not "compute each step fully, then pass the whole result on"?*
  Pulling one row at a time means operators compose uniformly and a query doesn't have to
  materialize giant intermediate tables. It's also the model real systems use and the easiest to
  explain. (Making it *faster* with vectorized/batched execution is literally Track A's job, so we
  deliberately left that out of our Track-B project.)

What you can write: `CREATE TABLE`, `INSERT`, `SELECT` (with `WHERE`, `AND`/`OR`, and a two-table
equi-`JOIN`), `DELETE`, and the transaction commands.

---

## 6. Optimizer

Before running a `SELECT`, the optimizer makes two decisions and prints what it picked (look for
the `[opt]` lines in the output) so you can see it think.

- **How selective is the filter?** Without real statistics we use simple rules: an equality match
  keeps about 10% of rows, a range (`<`, `>`, …) about a third, `AND` multiplies the fractions, and
  `OR` combines them. The one thing we *know* exactly is that the primary key is unique, so
  `pk = 3` matches exactly one row out of N.
- **Sequential scan or index scan?** We give each a rough cost: a sequential scan costs `N` (read
  every row), and an index scan costs `matches + log₂N` (descend the tree, then fetch the matches).
  The cheaper one wins. So `id = 3` uses the index, while `age > 25` (no index on `age`) or a tiny
  table just scans. Even when we use the index, a regular filter still sits on top, so the answer
  is correct no matter how wrong the estimate was — the estimate only affects *speed*, never
  correctness.
- **Which way to order a join?** Our nested-loop join keeps the inner table in memory, so the
  optimizer makes the **smaller** table the inner one to keep that memory small.

*Why heuristics instead of real histograms?* Histograms mean collecting and maintaining statistics
about the data, which is a whole subsystem. The rubric asks for selectivity estimation and a
scan/join choice, and heuristics demonstrate exactly that reasoning without the extra machinery —
and since the residual filter guarantees correctness, a rough estimate is a safe simplification.

---

## 7. Transactions & Concurrency

There are two related pieces here, and it's worth being clear about how they split up, because the
project keeps them separate on purpose.

**a) The concurrency engine (`txn/`, this is Track B).** This is a standalone, thread-safe
`TransactionManager` over an in-memory versioned store. It can run in two modes:

- **MVCC mode:** every write creates a new version of the row tagged with `{value, xmin, xmax}`
  (who created it, who deleted it). A reader sees the version that was committed as of its own
  snapshot, so readers never wait for writers.
- **Two-phase-locking mode:** writes take exclusive locks and reads take shared locks, and
  everything is released at commit/abort. This gives serializable isolation.

The *only* difference between the two modes is whether a read takes a lock — which is precisely the
thing the benchmark measures. The engine also does **deadlock detection**: it keeps a "who is
waiting on whom" graph and, whenever a transaction is about to block, runs a quick cycle check; if
blocking would create a cycle, that transaction is aborted so the others can proceed.

**b) SQL transactions (`Database`).** This is what `BEGIN`/`COMMIT`/`ROLLBACK` (and the implicit
auto-commit around a single statement) use in the SQL prompt. It gives **atomicity and durability**
through the WAL: a `ROLLBACK` undoes the statement's writes, and a `COMMIT` makes them durable.

*Why two separate systems instead of one?* The SQL prompt is a single connection — only one
statement runs at a time — so there's no concurrency to protect there; what it needs is
all-or-nothing writes and durability, which the WAL gives. Real *concurrency* (many threads,
locking, deadlocks, MVCC-vs-2PL) only makes sense with many transactions at once, so we exercise
it in the dedicated engine that the benchmark hammers from lots of threads. Splitting them keeps
each side simple and easy to explain.

**Isolation, stated honestly.** 2PL mode is serializable (the classic guarantee of two-phase
locking). MVCC mode gives snapshot isolation: readers never block, and because writers still take
exclusive locks, two transactions can't overwrite each other's update. That's the level of
guarantee the requirement asks MVCC to demonstrate.

**Deadlocks.** The transaction whose request would close a cycle in the waits-for graph is the one
that gets aborted; it releases its locks and the others continue.

---

## 8. Recovery

This is what keeps committed data safe if the program dies unexpectedly.

- **The write-ahead log (WAL).** Before we change any data, we append a record describing the
  change to a log file. An `INSERT` record carries the new row (so we can REDO it); a `DELETE`
  record carries the old row (so we can UNDO it). The golden rule — *write-ahead* — is that a
  transaction's records are flushed to disk at `COMMIT`, before its data pages are. That means even
  if a committed transaction's pages never made it out of the buffer pool, we can rebuild it from
  the log.
- **The records themselves** are small binary blobs, length-prefixed (`type · txid · pk · table ·
  row bytes`). If the program crashed halfway through writing the last record, the reader notices
  the record is too short and simply drops it.
- **Recovery (`RECOVER`).** On restart we read the whole log, mark every transaction that has a
  `COMMIT` record as a winner, then **REDO** every committed change (idempotently — insert only if
  missing, delete only if present) and **UNDO** any *loser* change that managed to reach the data.
  The result is a database that reflects exactly the committed work and nothing else.

Our log records whole rows ("insert this row" / "this row was deleted") rather than raw page
bytes. *Why log rows and not pages?* Logging rows is simpler to write and to reason about, and it
makes REDO safe to run more than once — we just check the index before re-applying, so replaying
the log twice does no harm. That gives us the write-ahead rule, the redo/undo passes, and real
crash-safety with code we can explain line by line. The `CRASH` command (a hard exit that skips
the buffer-pool flush) lets us prove it works — see §12.

---

## 9. Extension Track — B (MVCC)

- **Why it matters.** Under a read-heavy, contended workload, 2PL readers keep getting stuck behind
  writers' exclusive locks. MVCC's whole idea is that readers don't need locks at all — they read a
  consistent snapshot — so they sail past the writers. We trade strict serializability (MVCC gives
  snapshot isolation) for a big jump in read throughput.
- **How we built it.** Each key has a chain of versions tagged with `xmin`/`xmax`, plus a visibility
  rule that decides which version a given snapshot should see. Writers still take exclusive locks,
  so writes stay serialized and conflicts are caught; readers in MVCC mode take no lock. The exact
  same engine runs in 2PL mode (where readers *do* lock) so the comparison is truly apples-to-apples
  — same code, one switch flipped.
- **What we found.** See §10: MVCC sustains dramatically more reads under contention, and the gap
  grows as the writers hold their locks longer. That's exactly the behaviour Track B asks us to
  demonstrate.

---

## 10. Benchmarks

**The setup.** `benchmarks/bench_mvcc_vs_2pl.cpp` runs one identical workload — N reader threads
and M writer threads all fighting over a few hot keys, with each writer briefly holding its
exclusive lock — once in MVCC mode and once in 2PL mode, and counts how many reads finish in a
fixed time window. Build and run it with `./build.sh` then `./benchmarks/bench`; the raw output is
saved in `benchmarks/results/mvcc_vs_2pl.txt`.

**Results** (measured on our machine — absolute numbers depend a lot on the OS scheduler, so what
matters is the direction and scale of the gap):

| Workload | MVCC reads | 2PL reads | MVCC advantage |
|----------|-----------:|----------:|:--------------:|
| light  (8 readers, 2 writers, 200 µs hold)  | ~717,000 | ~3,000 | **~200×** |
| heavy  (12 readers, 4 writers, 500 µs hold) | ~385,000 | ~5,000 | **~70×**  |

**What it means.** When writers hold their locks longer (the "heavy" row), 2PL readers spend almost
all their time blocked waiting for those locks, so their throughput collapses. MVCC readers don't
care — they read a snapshot without locking — so their number barely moves. The headline is simple:
**under write contention, MVCC readers keep going while 2PL readers stall.** (Your exact ratios will
differ run to run; the shape is the point.)

---

## 11. Limitations

We kept the scope tight on purpose. Things we knowingly left out:

- **Types:** only `INT` and `TEXT`, no `NULL`, and integer literals are non-negative.
- **Queries:** two-table equi-joins only (nested loop, inner side held in memory); no aggregation,
  `GROUP BY`, `ORDER BY`, or `UPDATE`.
- **Index:** one primary-key B+ tree, in memory (rebuilt when a table opens); no secondary indexes.
- **Optimizer:** heuristic selectivity (no histograms); index scans only on the primary key.
- **Catalog isn't persisted:** reopening a database means re-running its `CREATE TABLE` statements
  (after that, the data and recovery work); recovery is triggered explicitly with `RECOVER`.
- **Concurrency:** the MVCC/2PL engine is driven by the benchmark; the SQL prompt is
  single-connection, so SQL transactions give atomicity/durability rather than isolation between
  concurrent SQL sessions.
- **Recovery:** we use the normal OS file flush, so we're safe across a program crash but not a
  sudden power loss; the whole log replays on recovery (no checkpoints to start from).

If we kept going: save the catalog so you don't re-type `CREATE TABLE`, store the B+ tree on disk,
add secondary indexes, and plan bigger joins.

---

## 12. How to Run

**You need** a C++17 compiler (`g++` or `clang++`) with pthreads. That's it — no external libraries.

**Build** (this produces `./minidb` and `./benchmarks/bench`):

```sh
./build.sh
```

If you'd rather not use the script, the build is a single command:

```sh
g++ -std=c++17 -O2 -pthread -Isrc $(find src -name '*.cpp') -o minidb
```

**Run the database.** It reads SQL from standard input, and stores each table's data file in the
directory you pass (here, `data`):

```sh
mkdir -p data
./minidb data <<'SQL'
CREATE TABLE emp (id INT PRIMARY KEY, name TEXT, age INT, dept_id INT);
CREATE TABLE dept (id INT PRIMARY KEY, dname TEXT);
INSERT INTO emp VALUES (1, 'Kartik', 20, 10);
INSERT INTO emp VALUES (2, 'Krritin', 30, 20);
INSERT INTO emp VALUES (3, 'Krishank', 25, 10);
INSERT INTO emp VALUES (4, 'Nitish', 17, 20);
INSERT INTO dept VALUES (10, 'Engineering');
INSERT INTO dept VALUES (20, 'Sales');
SELECT name FROM emp WHERE id = 1;                                  -- optimizer picks the index
SELECT name FROM emp WHERE age > 25;                                -- no index on age -> seq scan
SELECT emp.name, dept.dname FROM emp JOIN dept ON emp.dept_id = dept.id;
BEGIN;
INSERT INTO emp VALUES (7, 'Temp', 99, 10);
ROLLBACK;                                                           -- row 7 undone
SELECT id, name FROM emp WHERE id = 4;                              -- still consistent
SQL
```

**See crash recovery for yourself** (two separate runs of the program):

```sh
# 1) commit rows (1,2); start an uncommitted insert (3); then crash
./minidb data <<'SQL'
CREATE TABLE acct (id INT PRIMARY KEY, bal INT);
BEGIN; INSERT INTO acct VALUES (1,100); INSERT INTO acct VALUES (2,200); COMMIT;
BEGIN; INSERT INTO acct VALUES (3,300);
CRASH;
SQL

# 2) recover: the committed (1,2) come back, the uncommitted (3) is gone
./minidb data <<'SQL'
CREATE TABLE acct (id INT PRIMARY KEY, bal INT);
RECOVER;
SELECT * FROM acct;
SQL
```

**Run the Track B benchmark:**

```sh
./benchmarks/bench                  # default light workload
./benchmarks/bench 12 4 2 500 500   # readers writers keys window_ms write_hold_us
```

For a deeper tour, see `docs/architecture.md` (how a read, a write, and a recovery each flow
through the system) and `docs/design_decisions.md` (every trade-off as *what we chose → why → what
it costs us*).
