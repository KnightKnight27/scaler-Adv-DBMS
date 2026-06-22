# MiniDB — Design Decisions & Trade-offs

For each real decision we lay out the options we considered, the pros and cons of each, and which
one we picked and why. The rule we kept coming back to: build the simplest thing that's actually
correct, and be able to explain why we didn't build the fancier version.

---

## 1. Buffer pool eviction — which page do we kick out?

| Option | Pros | Cons |
|--------|------|------|
| **FIFO** (evict the oldest) | dead simple | ignores how often a page is used; can evict a hot page |
| **True LRU** (evict least recently used) | tracks usage exactly | must reorder a list on *every* access — fiddly bookkeeping |
| **Clock-sweep** | O(1), no list to maintain, resists being wiped by a big scan | only *approximates* "least recently used" |

**Our pick: clock-sweep.** Each page has a small counter; a "hand" sweeps around lowering counters
and evicts the first one that hits zero. It gets almost the same quality as LRU without the list
bookkeeping, which is why real databases use this family of algorithms.

---

## 2. Index structure — how do we find a row by key?

| Option | Pros | Cons |
|--------|------|------|
| **Hash index** | very fast for `key = value` | can't do ranges (`id > 100`) at all; no ordering |
| **Plain B-tree** | ordered; supports ranges | stores data in *every* node, so fewer keys fit per node and a range scan hops around the tree |
| **B+ tree** | ordered; all data in linked leaves, so ranges are a simple walk; high fan-out | separator keys are duplicated in internal nodes |

**Our pick: B+ tree.** We wanted range scans, which rules out hashing. Keeping all the data in the
leaves (and chaining the leaves) makes a range query just a walk along the bottom of the tree — the
reason nearly every database uses a B+ tree for its primary index.

---

## 3. Page layout — how are rows packed into a 4 KB page?

| Option | Pros | Cons |
|--------|------|------|
| **Fixed-length records** | trivial to address (`slot × size`) | wastes space for `TEXT`, or can't fit variable text at all |
| **Slotted page** (a directory of `(offset, length)` slots) | handles variable-length rows; a row's `RowID` stays valid even if its bytes move inside the page | a few bytes of slot overhead per row |

**Our pick: slotted page.** Our `TEXT` columns vary in length, so fixed-size slots wouldn't fit
well. The slot directory also gives us a stable `RowID = (page, slot)`, which is exactly what the
index needs to point at.

---

## 4. SQL parser — how do we turn text into a tree?

| Option | Pros | Cons |
|--------|------|------|
| **Parser generator** (yacc/bison/ANTLR) | handles big grammars; less hand-written code | extra tool + grammar file; the generated code isn't ours to explain |
| **Recursive-descent** (hand-written) | just readable functions we wrote; full control; easy to walk through in a viva | we code operator precedence by hand |

**Our pick: recursive-descent.** Our SQL is a small subset, so the hand-written parser stays short,
and there's no generated code or external tool to account for — every line is ours.

---

## 5. Query execution — how do operators pass rows along?

| Option | Pros | Cons |
|--------|------|------|
| **Materialize everything** (each step computes its full result, then hands it on) | simple to picture | builds big intermediate tables in memory |
| **Pull / Volcano** (each operator has `open()` + `next()`, pulls one row at a time) | operators compose uniformly; no giant intermediates; what real engines use | one function call per row (negligible) |

**Our pick: the pull (Volcano) model.** `SeqScan`, `IndexScan`, `Filter`, `Project`, and the join
all share the same `open()`/`next()` interface, so they snap together in any order and a query
streams rows instead of materializing whole tables. (Making this *faster* with batched/vectorized
execution is exactly what Track A is about, so we left it out of our Track-B project on purpose.)

---

## 6. Join algorithm — how do we combine two tables?

| Option | Pros | Cons |
|--------|------|------|
| **Nested-loop** | simplest; obviously correct; works for any condition | compares every left row with every right row |
| **Hash join** | fast for equi-joins on big tables | needs a hash table and more code; equi-joins only |
| **Sort-merge join** | good when inputs are already sorted | needs a sort step; more machinery |

**Our pick: nested-loop, with the smaller table held in memory.** For the small tables in our demos
it's plenty fast, and it's the join that's easiest to read and to defend. We make the *smaller*
relation the inner one so the in-memory side stays small.

---

## 7. Concurrency control — 2PL vs MVCC (this is Track B)

This is the heart of the extension, so instead of skipping one we **built both** and let the engine
switch between them.

| Option | Pros | Cons |
|--------|------|------|
| **Strict 2PL** (readers and writers take locks) | strongest guarantee (serializable) | readers wait behind writers' locks -> low read throughput under contention |
| **MVCC** (readers read a consistent snapshot, no lock) | readers never block writers -> much higher read throughput | slightly weaker guarantee (snapshot isolation); keeps old row versions around |

**Our pick: support both, default to MVCC, and benchmark them.** The two modes differ by exactly one
thing — whether a *read* takes a lock — so comparing them is fair (same code, one switch). That
comparison is the whole point of Track B, and the benchmark shows MVCC reads sailing past 2PL reads
when writers are holding locks.

---

## 8. Recovery logging — what do we write to the WAL?

| Option | Pros | Cons |
|--------|------|------|
| **Physical (raw page bytes)** | precise; how production systems (ARIES) do it | lots of bookkeeping; harder to explain |
| **Logical (whole rows)** — "insert this row" / "this row was deleted" | simple to write; REDO is safe to run twice (we check the index first) | coarser-grained than page-level logging |

**Our pick: logical, row-level logging.** It gives us the write-ahead rule, the REDO/UNDO passes,
and real crash-safety — with code we can explain line by line. Because REDO checks the index before
re-applying, replaying the log twice does no harm.

---

## Smaller calls (kept short on purpose)

- **4 KB pages** — matches the OS read size, so one page ≈ one disk read.
- **Delete marks the slot empty** (no compaction) — instant and simple; space isn't reclaimed.
- **Insert uses the first page with room** — simplest rule that works; may scan a few pages.
- **Index lives in memory, rebuilt from the heap on open** — no extra on-disk format; quick rebuild
  at startup; the heap is still the durable source of truth.
- **Rows stored as values only, in column order** (`INT` raw, `TEXT` = length + characters) — compact;
  the schema tells us how to read the bytes back.
- **Abort by visibility, not heap undo** — an aborted transaction is never marked committed, so its
  row versions simply become invisible.
- **`std::_Exit` to fake a crash** — exits instantly without flushing, just like a real crash, and
  it's repeatable for the demo.
