# PostgreSQL vs SQLite — Architecture Comparison

> Advanced DBMS — System Design Discussion
> Topic 1: PostgreSQL vs SQLite

I picked this topic because I have used both databases a little bit (SQLite came
built-in when I was learning Python, and PostgreSQL we used in a web project).
They both store tables and let me write SQL, but underneath they are built in
very different ways. This README is my attempt to understand *why* they are so
different.

---

## 1. Problem Background

Both PostgreSQL and SQLite are relational databases — you make tables, you write
SQL, you get rows back. But they were built to solve **two different problems**.

**SQLite** was created (around 2000, by D. Richard Hipp) to be a database that
needs *no server* and *no setup*. The whole database is just **one file** on disk.
The idea was: what if a program could just include a database directly inside
itself, like a library, instead of talking to a separate database server? That is
why SQLite is called an *embedded* database. It runs *inside* your application.

**PostgreSQL** comes from a much older university project (POSTGRES at Berkeley,
1980s, later it got SQL support and became PostgreSQL). Its goal was to be a
serious, full-featured database that **many users and many applications can
connect to at the same time** over a network. So it was built as a *client-server*
system — there is a database server running all the time, and clients connect to it.

So the one-line difference:
- **SQLite** = a database *library* that lives inside one app.
- **PostgreSQL** = a database *server* that many apps connect to.

---

## 2. Architecture Overview

### SQLite (embedded)

```
   +----------------------------------+
   |        Your Application          |
   |   (Python / Android app / etc.)  |
   |                                  |
   |   +--------------------------+   |
   |   |   SQLite library (C)     |   |   <- linked INSIDE the app
   |   |   - SQL parser           |   |
   |   |   - query executor       |   |
   |   |   - B-tree storage       |   |
   |   |   - pager                |   |
   |   +-----------+--------------+   |
   +---------------|------------------+
                   |
                   v
          +-----------------+
          |  database.db    |   <- ONE file on disk
          +-----------------+
```

There is no separate process. SQLite is just code running inside your program,
and it reads/writes one file.

### PostgreSQL (client-server)

```
  Client 1        Client 2        Client 3
 (psql / app)    (web server)    (app)
     |               |               |
     +-------+-------+-------+-------+
                     |  (network: TCP / sockets)
                     v
        +------------------------------+
        |     PostgreSQL Server        |
        |                              |
        |  Postmaster (main process)   |
        |    | forks one process       |
        |    | per client connection   |
        |    v                         |
        |  backend  backend  backend   |
        |                              |
        |  Shared Memory               |
        |   - shared buffers (cache)   |
        |   - WAL buffers              |
        |                              |
        |  Background processes:       |
        |   - WAL writer               |
        |   - checkpointer             |
        |   - autovacuum               |
        +--------------+---------------+
                       |
                       v
            +---------------------+
            |  Data files on disk |
            |  (many files)       |
            +---------------------+
```

Here the database is a **set of always-running processes**. When a client
connects, the main process (`postmaster`) creates a new **backend process** just
for that client.

### Process model — the biggest difference

| | SQLite | PostgreSQL |
|---|---|---|
| Runs as | library inside your app | separate server processes |
| One connection = | a file handle | a whole OS process (a backend) |
| Network | none (local file) | yes, clients connect over TCP/socket |
| Background workers | none | WAL writer, checkpointer, autovacuum, etc. |

---

## 3. Internal Design

### Storage structure / file organization

- **SQLite:** the *entire* database (all tables, all indexes, the schema) lives in
  **one single file**. Inside that file, data is stored in fixed-size **pages**
  (default 4 KB). The pages are organized as **B-trees** — one B-tree per table
  and one per index. There is a small header at the start of the file.

- **PostgreSQL:** uses a **whole directory** of files. Each table and each index
  usually gets its own file (and big tables get split into 1 GB chunks). Pages
  here are **8 KB** by default. A table's main file is called the **heap** — rows
  are just stored in the heap, not sorted, and indexes point into it.

A simple way I think about it:
- SQLite = everything zipped into one file.
- PostgreSQL = a folder full of files managed by a running server.

### Page layout (simple view)

Both store rows inside pages. A page roughly looks like:

```
+--------------------------------------------------+
| page header                                      |
| pointers ->  ->  ->                              |
|                                                  |
|              (free space in the middle)          |
|                                                  |
|                       <- row <- row <- row       |
+--------------------------------------------------+
```

Row pointers grow from the top, actual row data grows from the bottom, and the
free space is in between. PostgreSQL calls these row pointers the *line pointer
array*. This idea (slotted page) is actually similar in both systems.

### Index implementation

Both use **B-trees** as the default index.

- In SQLite, the table data itself is stored *in* a B-tree keyed by `rowid`
  (so the table is basically a B-tree). Indexes are separate B-trees that point
  back using the rowid.
- In PostgreSQL, the table (heap) is **not** sorted. Indexes are separate B-trees
  whose leaf entries point to a physical location in the heap (block number +
  item number, called a TID/ctid).

### Transaction management & durability

Both support **ACID transactions**, which surprised me — even tiny SQLite is fully
ACID. But the way they guarantee durability is different:

- **SQLite** historically used a **rollback journal** (write the old page contents
  to a side file before changing the real file, so it can undo on a crash). The
  newer and now common mode is **WAL (Write-Ahead Log)** — changes are first
  appended to a `-wal` file, then later merged into the main file.

- **PostgreSQL** always uses **WAL (Write-Ahead Logging)**. Before any change
  touches the actual data files, a record describing the change is written to the
  WAL. If the server crashes, on restart it replays the WAL to recover.

### Concurrency control — the part that matters most

This is where the two really split:

- **SQLite:** very simple. By default, writes lock the **whole database file**.
  Only **one writer at a time** is allowed. Readers can read concurrently (and in
  WAL mode readers don't block the single writer), but you still cannot have two
  writers at once. For a single app this is totally fine.

- **PostgreSQL:** uses **MVCC (Multi-Version Concurrency Control)**. Instead of
  locking rows for reading, it keeps **multiple versions of a row**. Each row has
  hidden columns (`xmin`, `xmax`) that say which transactions can see it. This
  means **readers never block writers and writers never block readers** — many
  users can read and write at the same time. This is essential for a multi-user
  server.

---

## 4. Design Trade-Offs

### SQLite

**Advantages**
- Zero configuration, no server to install or manage.
- The whole DB is one file → super easy to copy, email, back up, ship inside an app.
- Very small and fast for a single user / single app.
- Reliable and very well tested.

**Limitations**
- Only one writer at a time → bad for high write concurrency.
- Not built for many users over a network.
- No real user accounts / permissions like a big server has.
- Doesn't scale to huge multi-user systems.

### PostgreSQL

**Advantages**
- Handles **many concurrent users** well thanks to MVCC.
- Lots of features: rich SQL, JSON, extensions, advanced indexes, etc.
- Strong durability and crash recovery via WAL.
- Good for large, growing applications.

**Limitations**
- You have to install and run a server → more setup and maintenance.
- Heavier — uses more memory, one process per connection (so thousands of
  connections need a pooler).
- MVCC keeps old row versions, so it needs **VACUUM** to clean up dead rows
  (extra background work).
- Overkill for a small single-user app.

### Performance implications (how I understand it)
- For a single user doing local reads/writes, SQLite can actually be *faster*
  because there is no network and no process overhead.
- For 100 users hitting the DB at once, PostgreSQL wins easily because SQLite
  would serialize all the writes behind one lock.

---

## 5. Experiments / Observations

I tried small things on my laptop to *see* the difference.

**SQLite — it really is just a file:**
```sql
sqlite3 test.db
sqlite> CREATE TABLE students(id INTEGER PRIMARY KEY, name TEXT);
sqlite> INSERT INTO students(name) VALUES ('Anjali'), ('Ravi');
sqlite> .quit
```
After this, `test.db` exists as a single file. I could literally copy `test.db`
to a USB drive and it works elsewhere. No server was ever running.

**PostgreSQL — there is a server:**
```bash
# a server process must be running first
psql -d testdb
testdb=# CREATE TABLE students(id SERIAL PRIMARY KEY, name TEXT);
testdb=# INSERT INTO students(name) VALUES ('Anjali'), ('Ravi');
```
When I ran `ps aux | grep postgres` I could see several `postgres` processes
running in the background even when nobody was querying — the postmaster, the
WAL writer, the checkpointer, autovacuum, etc. That matched the diagram above.

**Observing MVCC hidden columns in PostgreSQL:**
```sql
SELECT xmin, xmax, * FROM students;
```
This actually shows the hidden `xmin` / `xmax` values on each row. SQLite has
nothing like this because it doesn't keep multiple row versions.

**Concurrency test idea:** if I open two connections to PostgreSQL, both can
update *different* rows at the same time. With SQLite, if one connection is
writing, the second writer gets a "database is locked" error. This is the single
clearest demonstration of the concurrency difference.

---

## 6. Key Learnings

- "Relational database" does **not** mean one fixed design. SQLite and PostgreSQL
  are both relational but architecturally almost opposite — embedded vs server.
- The **process model** (library vs client-server) is the root cause of most other
  differences (concurrency, setup, scalability).
- **MVCC** is the key trick that lets PostgreSQL serve many users at once, and the
  cost of MVCC is needing **VACUUM** to clean dead row versions.
- SQLite trades multi-user power for **simplicity** — and that simplicity is
  exactly why it's perfect for **mobile apps**, desktop apps, and browsers (it's
  literally embedded in phones, browsers, etc.). No server, one file, low memory.
- PostgreSQL trades simplicity for **scalability and features** — which is why it
  is preferred for **large multi-user systems** like web backends.
- Biggest takeaway: there is no "better" database here. Each one is the *right*
  answer to a *different* question. Engineering is about matching the design to
  the problem.

### Answers to the suggested questions
- **Why does SQLite work well for mobile apps?** Because it is embedded (no server
  process), the whole DB is one file, it uses little memory, and a mobile app
  usually has just *one* user → SQLite's single-writer limit doesn't hurt.
- **Why is PostgreSQL preferred for large multi-user systems?** Because client-
  server + MVCC let many users read and write concurrently without blocking each
  other, plus it has the features and durability big systems need.
- **What architectural decisions lead to these differences?** The choice of
  *embedded library vs client-server*, *one file vs many files*, and
  *whole-file locking vs MVCC*.
