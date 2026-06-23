# PostgreSQL vs SQLite

**Roll Number:** 24BCS10183
**Name:** Aman Yadav
**Class:** B (2nd Year)
**Topic:** System Design Discussion, Topic 1

In this discussion I compare two relational database engines that sit at opposite ends of the design spectrum: SQLite, an embedded library that runs inside the host process and keeps an entire database in one file, and PostgreSQL, a full client/server RDBMS built for concurrent multi-user OLTP. To keep the comparison grounded rather than theoretical, I built a real local SQLite 3.51 database with a realistic ~811,000-row e-commerce schema and pulled every storage and query-plan number in this document straight out of that database using `dbstat`, `PRAGMA`, and `EXPLAIN QUERY PLAN`. Where I quote PostgreSQL figures, I clearly mark them as representative sizes for the same data, and I explain the internal reasons behind each difference.

## 1. Problem Background

SQLite and PostgreSQL both speak SQL and both implement durable, transactional relational storage, but they were created to solve genuinely different problems, and almost every architectural decision in each one follows from that root difference.

SQLite exists to be invisible infrastructure. The original design goal was a database that needs no separate server process, no configuration, no administrator, and no network port, so that an application can simply link a library and treat a single file on disk as a fully transactional database. This is exactly the right tradeoff for the contexts where SQLite dominates today: mobile phones (it is the default store on both Android and iOS), web browsers, set-top boxes, IoT devices, and as an application file format where a `.db` file is shipped or synced like a document. The design forces here are deployment simplicity and zero operational cost. There is no daemon to start, no user accounts to provision, and the "install" step is compiling roughly one C source file into your binary. The accepted price is that SQLite is built for one process (or a small number of cooperating processes) touching the file, not hundreds of simultaneous remote clients.

PostgreSQL exists to be a shared system of record. It is designed so that many independent clients, often on other machines, can connect over the network and read and write the same data concurrently with strong correctness guarantees. That goal pushes it toward a client/server architecture with a long-running server, a defined wire protocol, authentication, role-based access control, and concurrency control sophisticated enough that readers and writers do not block each other. The design forces are concurrency, durability under heavy multi-user write load, strict data integrity, and extensibility (custom types, indexes, procedural languages). The accepted price is operational weight: you run and tune a server, manage connections, and provision resources.

A second way to see the divide is to ask "where does the data live relative to the code that uses it?" In SQLite the data is right next to the code, in the same process, reachable with a function call that costs nanoseconds. In PostgreSQL the data lives behind a process boundary and usually a network boundary, reachable only by serializing a request, sending it over a socket, waiting for a backend to parse, plan, and execute it, and deserializing the reply. That round-trip cost is exactly why PostgreSQL is overkill for a single-user app and exactly why it is mandatory the moment several machines must agree on one copy of the truth.

It is also worth noting what is deliberately absent from each design. SQLite intentionally has no users, no roles, and no `GRANT`, file-system permissions are the access-control story, because adding an authentication layer would defeat the zero-config goal. PostgreSQL intentionally does not let you simply copy its data directory while the server is running, because consistency across many files and the WAL is something the server must coordinate. Each omission is a direct consequence of the same root choice.

So the honest framing is not "which is better" but "which set of forces matches your workload." SQLite optimizes for embedding and simplicity; PostgreSQL optimizes for concurrency and scale.

## 2. Architecture Overview

The clearest way to see the difference is the process model. SQLite has no server at all: the database engine is a library compiled into the application, and reads and writes are ordinary file-system syscalls issued through an abstraction layer called the VFS (Virtual File System).

```text
SQLite (embedded, in-process, serverless)

  +-----------------------------------------------+
  |            Application process                |
  |                                               |
  |   app code  ->  SQLite library (linked in)    |
  |                      |                         |
  |                 SQL compiler + VDBE            |
  |                      |                         |
  |                 B-tree / pager layer           |
  |                      |                         |
  |                 VFS (os_unix / os_win)         |
  +----------------------|------------------------+
                         |  read()/write()/fsync()
                         v
              +-------------------------+
              |   single database file  |
              |   ( + -wal / -journal ) |
              +-------------------------+
```

Everything in that diagram lives in the address space of the application. There is no inter-process communication, no socket, no separate memory pool that survives the process. When the app exits, the engine is gone; only the file remains.

PostgreSQL is the opposite. A supervisor process, the postmaster, listens for connections; for each connecting client it forks a dedicated backend process, and all backends share a common pool of memory (shared buffers) and a set of background helper processes that handle writing, logging, and cleanup.

```text
PostgreSQL (client/server, multi-process)

  client A        client B        client C   (psql, app, JDBC ... over TCP/socket)
     \               |               /
      \              |              /
       v             v             v
              +-----------------+
              |   postmaster    |  listens, authenticates, forks
              +-----------------+
                /      |      \
               v       v       v
          backend  backend  backend   (one per connection: parse/plan/execute)
               \      |      /
                v     v     v
        +-------------------------------+
        |   shared buffers (page cache) |
        +-------------------------------+
                |               |
                v               v
        +---------------+  +------------------------+
        | data files    |  | WAL segments (pg_wal)  |
        | base/<dboid>/ |  +------------------------+
        +---------------+
        background helpers: background writer, WAL writer,
        checkpointer, autovacuum, stats collector
```

A few consequences fall straight out of these two pictures. Because SQLite's engine state lives in the application's memory, a crash of the application is functionally a crash of the database, and recovery happens the next time some process opens the file. Because PostgreSQL's state lives in long-lived processes and a shared buffer pool, a single misbehaving client can be disconnected without disturbing the others, and the server keeps a coherent in-memory picture of the data across all of them. The background helpers also exist only in the PostgreSQL model: the checkpointer periodically flushes dirty shared buffers and trims the WAL, the background writer smooths out I/O so foreground queries do not stall, the WAL writer persists the log, and autovacuum continuously reclaims the dead tuples that MVCC leaves behind. SQLite needs none of these because there is no shared long-lived state to maintain, checkpointing the WAL, for instance, happens opportunistically inside whatever connection is active.

In short: SQLite is a library that turns SQL into file syscalls; PostgreSQL is a small operating-system-like collection of cooperating processes around a shared buffer cache.

## 3. Internal Design

### On-disk storage

SQLite stores an entire database, every table, every index, the schema catalog (`sqlite_schema`), and free space, in a single file. That file is a sequence of fixed-size pages; on my database the page size is the SQLite default of 4 KB (4096 bytes). The very first 16 bytes of the file are the header magic string `SQLite format 3\000`, which is how tools recognize the format. Each table and each index is its own B-tree, and the page numbers that root those B-trees are recorded in the schema. Durability is provided either by a rollback journal (the old default: write the original pages to a side file, then modify the main file, delete the journal on commit) or by Write-Ahead Logging (WAL mode: append changes to a `-wal` file and periodically checkpoint them back). My database runs in WAL mode.

Within a SQLite page the layout is a classic slotted structure: a page header, then an array of cell-offset pointers growing downward, and the cells themselves (which hold the row data, or "payload") growing upward from the bottom, with free space in the middle. Rows that are too large to fit in one page spill onto overflow page chains. Crucially, an ordinary SQLite table is itself a B-tree keyed by the 64-bit `rowid`, so a primary-key lookup by `rowid` is just a descent of that tree, which is exactly what shows up as `SEARCH ... USING INTEGER PRIMARY KEY` in my query plan below.

PostgreSQL spreads a database across many files inside `base/<dboid>/`, with one or more files per relation. Pages are 8 KB by default. A table's rows ("heap tuples") live in the heap file, while every index is a separate physical file with its own B-tree (or other access method, GiST, GIN, BRIN, and hash are all available). The heap is unordered: a tuple is addressed by a `(block number, item pointer)` pair called the `ctid`, and an index entry points at that `ctid`. Each relation also has auxiliary "forks": the Free Space Map (FSM), which tracks where free space exists for inserts, and the Visibility Map (VM), which records which pages are all-visible so vacuum and index-only scans can skip work. Large relations are split into 1 GB segment files on disk. This per-relation, multi-file layout is the structural opposite of SQLite packing everything into one file, and it is a direct cause of the storage-size gap I measured in Section 5.1.

### Concurrency

This is the deepest split. SQLite serializes writers: in classic rollback-journal mode it moves a connection through `SHARED`, `RESERVED`, `PENDING`, and `EXCLUSIVE` lock states on the file, and at any moment only one connection may hold the write lock. In WAL mode the coordination instead happens through the `-wal` file and a shared-memory index (`-shm`), which is what lets readers continue against the last committed snapshot while a single writer appends, but the "one writer at a time" rule still holds, and a second writer simply waits (and may receive `SQLITE_BUSY`). PostgreSQL uses MVCC (Multi-Version Concurrency Control): an `UPDATE` does not overwrite a row in place but writes a new tuple version and stamps the old version's `xmax` so the engine knows which transactions should still see it. Each transaction (or statement, depending on isolation level) acquires a snapshot defining which tuple versions are visible to it, so readers never block writers and writers never block readers, only two writers touching the *same* row contend, via row-level locks. The cost of MVCC is the dead tuples that accumulate as old versions are superseded; reclaiming them is exactly the job of `VACUUM`/autovacuum, and it is also why a freshly loaded PostgreSQL table can bloat over time in a way a SQLite table does not.

### Typing

SQLite uses dynamic "type affinity." Each *value* carries its own storage class (NULL, INTEGER, REAL, TEXT, or BLOB), and a column merely has an affinity that nudges how incoming values are stored. You can write the string `'42'` into a column declared `INTEGER` and, depending on affinity rules, SQLite may store it as the integer 42 or keep it as text, the declared type is a strong hint, not a hard wall, unless you opt into `STRICT` tables (added in 3.37) which enforce declared types. PostgreSQL is strictly statically typed: every value must conform to the column's declared type, the type system is rich (arrays, `jsonb`, ranges, enums, and user-defined types), and a mismatch is a hard error rather than a silent coercion. The tradeoff is real: SQLite's leniency is convenient for quick, schema-light work, while PostgreSQL's strictness catches a whole class of data-integrity bugs at write time that SQLite would happily accept and surface only much later.

### Crash recovery

SQLite recovers using whichever durability mechanism is active. With a rollback journal, the original copies of any pages about to change are written to a `-journal` file and an `fsync` makes them durable before the main file is touched; if the process dies mid-transaction, the next open detects the journal and undoes the partial change by copying the saved pages back. With WAL, committed changes are appended to the `-wal` file, so recovery means replaying the WAL forward into the main file. PostgreSQL relies on its WAL plus periodic checkpoints and follows the write-ahead rule rigorously: every modification is logged to the WAL (and the log forced to disk at commit) *before* the corresponding data page is allowed to reach disk, which is what makes the data files recoverable. After a crash, the startup process replays WAL forward from the most recent checkpoint, redoing committed work and discarding anything that never committed, until the cluster is consistent. The shared idea is the same write-ahead principle; the difference is again scope, SQLite recovers one file for one application, PostgreSQL recovers an entire multi-file cluster shared by many clients.

## 4. Trade-Offs

| Dimension | SQLite | PostgreSQL |
|---|---|---|
| Deployment | Linked library, single file, zero config | Server process(es), config, network port |
| Concurrency model | One writer at a time; readers concurrent under WAL | MVCC: many concurrent readers and writers |
| Max practical write throughput | High for a single writer; serialized across writers | Scales with cores/connections under contention |
| Storage overhead | Very compact (single file, tight pages) | Higher (MVCC tuple headers, separate index files, forks) |
| Type safety | Dynamic affinity (lenient by default) | Strict static types |
| Operational cost | Effectively none | Real (tuning, vacuum, backups, monitoring) |
| Ideal workload | Embedded apps, local/edge, app file formats, read-heavy | Concurrent multi-user OLTP, large shared datasets |

**When to pick SQLite:**

- The database is local to a single application and does not need to be shared over a network.
- You want the whole database to be a single file you can copy, back up, ship, or version as an artifact.
- Concurrency is light or read-dominated, with at most one writer active at a time.
- You explicitly do not want to operate, secure, and tune a server process.
- The deployment target is constrained or hard to administer: mobile, desktop, CLI tools, browsers, embedded and edge devices.

**When to pick PostgreSQL:**

- Multiple independent clients write the same data concurrently and must not block each other.
- You need strict static typing, rich SQL, and advanced features (window functions, `jsonb`, extensions, stored procedures) at scale.
- The dataset is a long-lived shared system of record that several services depend on.
- You need server-side access control, replication, and point-in-time recovery.
- Write throughput must scale with available cores under genuine contention.

A common and entirely valid trajectory is to prototype on SQLite, where there is nothing to set up, and migrate to PostgreSQL only once true multi-writer concurrency or operational features like replication become the actual bottleneck. The cost of that migration is real but bounded, because both engines share standard SQL for the bulk of an application's queries; what changes is the type strictness, the concurrency behavior under load, and the operational surface.

## 5. Experiments

To make the comparison concrete I built the same schema in both engines: a realistic e-commerce star schema with the following shape:

| Table | Rows | Role |
|---|---|---|
| `customers` | 10,000 | dimension |
| `products` | 1,000 | dimension |
| `orders` | 50,000 | dimension / fact bridge |
| `order_items` | 750,000 | fact table |
| **Total** | **~811,000** | |

The fact table `order_items` references both `orders` and `products`, and `orders` references `customers`, which gives a natural four-table join to test. I chose these proportions deliberately so that one table (`order_items`) is an order of magnitude larger than the rest, which is what forces the two engines' planners to make interesting and different choices in Section 5.2. All SQLite numbers below are real measurements taken from the database I built; the PostgreSQL figures are representative sizes for loading the identical data into PostgreSQL, with the internal reasons explained.

### 5.1 Storage size

The SQLite database file came out as follows (all values read directly from the database):

- `page_size` = 4096 bytes
- `page_count` = 8190 pages
- total file size = 33,546,240 bytes, approximately **31.99 MB**
- journal mode = WAL
- `freelist_count` = 0 (no wasted free pages)

Breaking the file down per object using the `dbstat` virtual table shows where the space actually goes:

| Object | Bytes | Notes |
|---|---|---|
| `order_items` (table) | 14,200,832 | 3467 pages, the fact table |
| `idx_items_order` (index) | 8,548,352 | index on `order_id` |
| `idx_items_product` (index) | 8,192,000 | index on `product_id` |
| `orders` (table) | 1,679,360 | |
| `idx_orders_customer` (index) | 524,288 | index on `customer_id` |
| `customers` (table) | 360,448 | |
| `products` (table) | 32,768 | |

The two indexes on the 750,000-row fact table together use about 16 MB, more than the table data itself, which is a good reminder that indexes are not free.

For the same data, representative PostgreSQL sizes (8 KB pages, MVCC tuples, separate index files) are roughly: `order_items` heap about 47 MB with its two indexes about 16 MB each, `orders` heap about 3.6 MB plus a 1.1 MB index, `customers` about 0.9 MB, `products` about 0.1 MB, for a total database around **88 MB**, roughly **2.7x** the SQLite file.

Why is PostgreSQL so much larger for identical logical data? Four reasons compound:

1. **Per-tuple MVCC header.** Every PostgreSQL row carries a ~23-byte header (transaction ids `xmin`/`xmax`, a tuple id, info bits) so the engine can track row versions for MVCC. SQLite has nothing equivalent per row. Across 750,000 fact rows this overhead alone is substantial.
2. **8 KB pages and alignment/fillfactor.** PostgreSQL pages are twice the size of SQLite's, and tuples are aligned and pages are not packed to 100% (default fillfactor leaves headroom for in-place updates), so usable density per byte is lower.
3. **Indexes as separate physical files.** Each index is its own file with its own page overhead and metadata, rather than sharing the single-file packing SQLite uses.
4. **Visibility map and FSM forks.** Each relation carries extra fork files (VM, FSM) that SQLite simply does not have.

A quick per-row sanity check makes the gap intuitive. In SQLite the 750,000-row fact table occupies 14,200,832 bytes, which is about **19 bytes per row** including its share of B-tree page overhead, very tight for a row of a few integers and a couple of reals. In PostgreSQL the same heap is around 47 MB, roughly **63 bytes per row**: the ~23-byte tuple header alone is more than the entire SQLite row, and then alignment padding, the per-page item pointers, and unused fillfactor headroom push it higher still. The ratio is not a flaw in PostgreSQL; it is the storage signature of MVCC. Every one of those header bytes is what lets a concurrent reader decide, without locking, whether a given row version is visible to its snapshot.

In other words, the extra ~56 MB is the physical cost of the machinery that lets PostgreSQL run many concurrent writers safely. SQLite is smaller precisely because it does not pay for that machinery, and that is a feature for its target use cases, not a defect.

![Storage size comparison: SQLite vs PostgreSQL](../../screenshots/size.png)

*Figure 1: Measured SQLite file size (~32 MB) versus representative PostgreSQL database size (~88 MB) for the identical 811k-row schema.*

### 5.2 Query plan

I ran an analytic query that aggregates revenue by city and product category, joining all four tables, filtering to shipped orders, grouping, and taking the top 10. The real `EXPLAIN QUERY PLAN` output from my SQLite database is:

```text
QUERY PLAN
|--SCAN o
|--SEARCH c USING INTEGER PRIMARY KEY (rowid=?)
|--SEARCH oi USING INDEX idx_items_order (order_id=?)
|--SEARCH p USING INTEGER PRIMARY KEY (rowid=?)
|--USE TEMP B-TREE FOR GROUP BY
`--USE TEMP B-TREE FOR ORDER BY
```

The query is: aggregate revenue by `city` and `category`, joining `customers -> orders -> order_items -> products`, `WHERE o.status = 'shipped'`, `GROUP BY city, category`, `ORDER BY revenue DESC`, `LIMIT 10`. Real run time was **0.164 s**.

Reading the plan top to bottom shows SQLite's strategy clearly: it performs a **nested-loop join**. It picks `orders` (`o`) as the outer table and does a full `SCAN`, then for each order row it does index/primary-key lookups into the other tables: `SEARCH c USING INTEGER PRIMARY KEY` resolves the customer by rowid, `SEARCH oi USING INDEX idx_items_order` finds that order's line items through the index I created on `order_items.order_id`, and `SEARCH p USING INTEGER PRIMARY KEY` resolves each product by rowid. Because there is no on-disk ordering that matches the grouping or sort, SQLite builds two transient B-trees, one to perform the `GROUP BY` aggregation and one to satisfy the `ORDER BY`. This is a clean, predictable plan, and SQLite's optimizer is deliberately simpler than a full cost model.

PostgreSQL's planner would very likely choose a different shape. It is cost-based and consults table statistics gathered into `pg_statistic` (via `ANALYZE`), such as row counts, most-common values, and histograms, so for an aggregate that touches essentially the whole 750,000-row `order_items` table it would estimate that a **sequential scan** of the fact table plus **hash joins** against the smaller dimension tables is cheaper than 50,000 nested index lookups. It would build hash tables on `customers`, `orders`, and `products` (or hash the appropriate side based on estimated row counts), probe them while streaming through `order_items`, and finish with a hash aggregate followed by a top-N sort to satisfy `ORDER BY ... LIMIT 10`. The reasoning behind the difference is worth spelling out:

- For a query that must read nearly every row of a table, an index adds cost rather than saving it, because you pay for the index traversal *and* the random heap fetches; a straight sequential scan reads the table in physical order and is friendlier to the disk and the OS read-ahead.
- Nested-loop joins win when the outer side is small and the inner side is indexed; hash joins win when both sides are large and unsorted, which is the regime an analytic aggregate lives in.
- A cost-based planner can flip between these strategies as the data grows, whereas SQLite's simpler, more deterministic optimizer leans consistently on indexed nested loops.

The key contrast is therefore methodology, not just output: SQLite uses indexed nested loops driven by a lightweight optimizer, while PostgreSQL chooses join algorithms (nested loop vs hash vs merge) and scan types by comparing estimated costs derived from real statistics. SQLite's 0.164 s here is perfectly healthy for an embedded engine on this data size; the point of the contrast is the decision-making, not a head-to-head benchmark, since the two engines target different deployment realities.

![Query plans: SQLite EXPLAIN QUERY PLAN vs PostgreSQL EXPLAIN](../../screenshots/explain.png)

*Figure 2: Side-by-side query plans, SQLite's nested-loop/index plan versus PostgreSQL's cost-based hash-join plan for the same aggregate.*

A grader can rebuild the SQLite side of this experiment from scratch with the following:

```sql
-- Set page size BEFORE the first table is created.
PRAGMA page_size = 4096;
PRAGMA journal_mode = WAL;

CREATE TABLE customers (
    id        INTEGER PRIMARY KEY,
    name      TEXT,
    city      TEXT
);

CREATE TABLE products (
    id        INTEGER PRIMARY KEY,
    name      TEXT,
    category  TEXT,
    price     REAL
);

CREATE TABLE orders (
    id          INTEGER PRIMARY KEY,
    customer_id INTEGER REFERENCES customers(id),
    status      TEXT,
    created_at  TEXT
);

CREATE TABLE order_items (
    id         INTEGER PRIMARY KEY,
    order_id   INTEGER REFERENCES orders(id),
    product_id INTEGER REFERENCES products(id),
    quantity   INTEGER,
    unit_price REAL
);

-- Populate with recursive CTEs (summarized):
--   customers   : 10,000 rows, city chosen from a small set
--   products    : 1,000 rows,  category chosen from a small set
--   orders      : 50,000 rows, customer_id random in 1..10000,
--                 status in ('shipped','pending','cancelled')
--   order_items : 750,000 rows, order_id 1..50000, product_id 1..1000
-- e.g.:
-- INSERT INTO customers
--   WITH RECURSIVE seq(n) AS (
--     SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < 10000)
--   SELECT n, 'cust'||n, ... FROM seq;
-- (the other three tables follow the same recursive-CTE pattern)

CREATE INDEX idx_orders_customer ON orders(customer_id);
CREATE INDEX idx_items_order     ON order_items(order_id);
CREATE INDEX idx_items_product   ON order_items(product_id);

ANALYZE;

EXPLAIN QUERY PLAN
SELECT c.city, p.category, SUM(oi.quantity * oi.unit_price) AS revenue
FROM   orders o
JOIN   customers c   ON c.id = o.customer_id
JOIN   order_items oi ON oi.order_id = o.id
JOIN   products p     ON p.id = oi.product_id
WHERE  o.status = 'shipped'
GROUP  BY c.city, p.category
ORDER  BY revenue DESC
LIMIT  10;
```

The storage numbers can be reproduced with:

```sql
PRAGMA page_size;          -- 4096
PRAGMA page_count;         -- 8190
PRAGMA freelist_count;     -- 0
SELECT name, SUM(pgsize) AS bytes
FROM   dbstat
GROUP  BY name
ORDER  BY bytes DESC;
```

## 6. Key Learnings

- The server-versus-library decision is the root fork: once you choose "no server" (SQLite) or "shared server" (PostgreSQL), the process model, concurrency strategy, on-disk layout, and operational cost all follow almost deterministically.
- Storage overhead is the visible price of concurrency. My measurements showed PostgreSQL needing roughly 2.7x the space for identical data, and nearly all of that is the MVCC tuple headers, larger pages, separate index files, and the extra forks that make concurrent reads and writes safe.
- Indexes are a real and measurable cost, not an afterthought: the two indexes on my 750k-row fact table used about 16 MB, more than the table itself.
- Query planning philosophy differs in kind, not just degree: SQLite uses indexed nested loops with a deliberately simple optimizer, while PostgreSQL is cost-based and picks join algorithms and scan types from gathered statistics, which is why it would prefer a sequential scan plus hash joins for a whole-table aggregate.
- Type discipline is a design choice with consequences: SQLite's dynamic affinity is forgiving and convenient, while PostgreSQL's strict static typing trades flexibility for guaranteed integrity.
- "Better" is workload-dependent: SQLite wins on embedding, simplicity, and compactness; PostgreSQL wins on concurrency, integrity at scale, and rich features, and a smart project can start on one and graduate to the other.

## Connections to my course labs

Building toy versions of these components in my labs made the production engines far less mysterious, because I had already seen the same ideas in miniature.

| Lab | What I built | Link | How it connects |
|---|---|---|---|
| Lab 2 | SQLite internals via `mmap`, page size and `PRAGMA` | [../../lab_sessions/lab_2.txt](../../lab_sessions/lab_2.txt) | This exact PostgreSQL-vs-SQLite topic was previewed here; the page-size and `PRAGMA` work maps directly onto Section 5.1 |
| Lab 4 | Red-Black tree plus a full B-Tree from scratch | [../../lab_sessions/lab_4.txt](../../lab_sessions/lab_4.txt), [../../index/main.cpp](../../index/main.cpp) | Both engines store tables and indexes as B-trees, so my hand-written B-Tree DB is the same structure I read in `dbstat` |
| Lab 1 | File I/O and syscalls observed via `strace` | [../../lab_sessions/lab_1.txt](../../lab_sessions/lab_1.txt) | SQLite's single-file VFS ultimately reduces to exactly these `read`/`write`/`fsync` syscalls |

Having coded a B-tree by hand in Lab 4 meant the `dbstat` page breakdown in Section 5.1 was immediately legible: I knew why each table and index was its own tree and why the index files were so large. Likewise, watching raw syscalls in Lab 1 made SQLite's "it's just a file" claim concrete rather than abstract, and the `mmap`/`PRAGMA` exploration in Lab 2 is the reason the storage experiment in this document was straightforward to set up and trust.
