# Lab 2 — SQLite3 vs PostgreSQL

Role Number: 24BCS10079
Name: Piyush Bansal

Done on my Mac (Apple Silicon, macOS Darwin 25.0.0). SQLite version 3.51.0 (the one that ships with macOS at `/usr/bin/sqlite3`). For PostgreSQL I ran 16.x via brew.

For the data I didn't want to seed a fake table because the timings would just be noise. Grabbed the IMDb `title.ratings.tsv` dataset from their public page (https://datasets.imdbws.com/) — about **1.67 million rows**, 3 columns (tconst, averageRating, numVotes). Same dataset on both sides, so the comparison is apples-to-apples.

```
curl -L -O https://datasets.imdbws.com/title.ratings.tsv.gz
gunzip title.ratings.tsv.gz
wc -l title.ratings.tsv
# 1667512   (1 header + 1667511 rows)
```


## 1. SQLite3 Exploration

### Loading the dataset

SQLite was already on the Mac (`/usr/bin/sqlite3`). Created a fresh DB and imported the tsv:

```
sqlite3 imdb.db
> CREATE TABLE ratings (tconst TEXT PRIMARY KEY, averageRating REAL, numVotes INTEGER);
> .mode tabs
> .import --skip 1 title.ratings.tsv ratings
> SELECT COUNT(*) FROM ratings;
1667511
```

### File size — `ls -lh`

```
$ ls -lh imdb.db
-rw-r--r--  1 piyushbansal  staff    78M  May  9 15:42 imdb.db
```

So the database file is 78 MB.

### PRAGMA inspection

```
sqlite> PRAGMA page_size;       -> 4096
sqlite> PRAGMA page_count;      -> 19952
sqlite> PRAGMA freelist_count;  -> 0
sqlite> PRAGMA mmap_size;       -> 0
sqlite> PRAGMA cache_size;      -> 2000
sqlite> PRAGMA journal_mode;    -> delete
```

Quick check: `4096 * 19952 = 81,723,392 bytes ≈ 78 MB`. Matches the file size from `ls -lh` exactly. So the SQLite file really is just `page_size × page_count` bytes — there's no separate header or metadata file you have to account for.

Also `mmap_size` is 0 by default, so out of the box SQLite is **not** using memory-mapped I/O — it's doing regular `pread()` syscalls. You have to opt in.

### Playing with `mmap_size`

Set up a small workload of 3 queries (count, aggregate, filter+sort+limit) and ran the whole batch 3 times under `time`:

```
$ time sqlite3 imdb.db "PRAGMA mmap_size=0;
    SELECT COUNT(*) FROM ratings;
    SELECT AVG(averageRating), SUM(numVotes) FROM ratings;
    SELECT tconst, averageRating FROM ratings
      WHERE numVotes > 100000
      ORDER BY averageRating DESC LIMIT 5;"
```

**Without mmap (`mmap_size=0`):**

| run | real time |
|-----|-----------|
| 1   | 0.59 s    |
| 2   | 0.24 s    |
| 3   | 0.24 s    |

**With mmap (`mmap_size=268435456`, i.e. 256 MB):**

| run | real time |
|-----|-----------|
| 1   | 0.24 s    |
| 2   | 0.24 s    |
| 3   | 0.24 s    |

The pattern was clear. Without mmap, run 1 was slow (0.59 s) because the OS had to actually pull the file off disk. Runs 2 and 3 were fast because the OS file cache had the pages in RAM. **With mmap, even run 1 was already at 0.24 s.** Once everything is hot, both modes converge.

So mmap basically makes the cold case behave like the warm case. Once the file is in cache it doesn't matter which mode you're in.

### `ps aux | grep sqlite`

While a query was running:

```
$ ps aux | grep sqlite | grep -v grep
piyushbansal 21313 9.7 0.1 435305216 6192 ?? RN 3:46PM 0:00.12
  sqlite3 imdb.db PRAGMA mmap_size=268435456; SELECT * FROM ratings LIMIT 1000000;
```

VSZ shows **~435 GB** which looks insane, but that's just how macOS reports virtual address space — it's not physical memory. RSS (the actual resident memory) was around **6 MB**. With `mmap_size=0` RSS was similar, since SQLite is just streaming rows out one at a time, not loading the whole file into memory.


## 2. PostgreSQL Setup

### Install

```
brew install postgresql@16
brew services start postgresql@16
createdb lab
```

`psql -d lab -c "SELECT version();"` reports PostgreSQL 16.x (Homebrew).

### Loading the same dataset

```
psql -d lab
lab=# CREATE TABLE ratings (
        tconst TEXT PRIMARY KEY,
        averageRating REAL,
        numVotes INTEGER);
lab=# \copy ratings FROM 'title.ratings.tsv' WITH (FORMAT csv, DELIMITER E'\t', HEADER true);
COPY 1667511
```

### Page size and page count

Postgres calls them blocks but it's the same idea. Default is 8 KB, and unlike SQLite this is a **compile-time** setting — you can't change it on a running cluster.

```
lab=# SHOW block_size;
 8192

lab=# ANALYZE ratings;
lab=# SELECT relname, relpages, reltuples::bigint
       FROM pg_class WHERE relname = 'ratings';
 relname | relpages | reltuples
---------+----------+-----------
 ratings |    10672 |   1667511
```

So `10672 × 8192 = ~83 MB` for the heap. Confirmed:

```
lab=# SELECT pg_size_pretty(pg_relation_size('ratings'))       AS table_size,
             pg_size_pretty(pg_total_relation_size('ratings')) AS total_size;
 table_size | total_size
------------+------------
 83 MB      | 134 MB
```

The 134 MB total includes the primary-key index. Found the actual data file too:

```
$ psql -d lab -At -c "SELECT pg_relation_filepath('ratings');"
base/16384/16385

$ ls -lh /opt/homebrew/var/postgresql@16/base/16384/16385*
-rw-------  ...  83M  16385
-rw-------  ...  40K  16385_fsm
```

### Query timing

Ran the same 3 queries with `\timing on`, 3 times:

```
lab=# \timing on
lab=# SELECT COUNT(*) FROM ratings;
lab=# SELECT AVG(averageRating), SUM(numVotes) FROM ratings;
lab=# SELECT tconst, averageRating FROM ratings
        WHERE numVotes > 100000 ORDER BY averageRating DESC LIMIT 5;
```

| run | COUNT(*) | AVG/SUM | filtered top-5 |
|-----|----------|---------|----------------|
| 1   | 31 ms    | 50 ms   | 44 ms          |
| 2   | 37 ms    | 50 ms   | 39 ms          |
| 3   | 27 ms    | 47 ms   | 37 ms          |

So all 3 queries together run in roughly 110–130 ms. There's no big cold-vs-warm gap like SQLite had, probably because the postgres server was already running with the buffer pool warmed up.

### `ps aux | grep postgres`

```
postgres -D /opt/homebrew/var/postgresql@16
postgres: checkpointer
postgres: background writer
postgres: walwriter
postgres: autovacuum launcher
postgres: logical replication launcher
```

Big architectural difference vs SQLite. Postgres is a real multi-process server — there's a parent (postmaster), background helpers (checkpointer, bgwriter, walwriter, etc.), and one backend per client connection. SQLite is just a library inside whatever process opens the .db file. No daemon, no IPC.

Postgres has no `mmap_size`. Closest equivalent is `shared_buffers`:

```
lab=# SHOW shared_buffers;        -- 128MB
lab=# SHOW effective_cache_size;  -- 4GB
```

Instead of mmap-ing the file, postgres reads through the OS in the normal way but keeps copies of hot pages in its **own** in-process pool of 128 MB. So the equivalent "give me more cache" knob is bumping `shared_buffers`, not setting an mmap size.


## 3. Comparison: SQLite vs PostgreSQL

### Page size

|              | SQLite                                              | Postgres        |
|--------------|-----------------------------------------------------|-----------------|
| default      | 4 KB (4096 B)                                       | 8 KB (8192 B)   |
| changeable?  | yes, `PRAGMA page_size = N` before DB creation      | no, compile-time|

Postgres uses a bigger page so it pulls 2x the data per I/O. SQLite's smaller page is fine for embedded / small-DB workloads.

### Page count

For the same 1.67M rows:

|                                | pages   | bytes/page | total  |
|--------------------------------|---------|------------|--------|
| SQLite                         | 19,952  | 4096       | 78 MB  |
| Postgres (heap only)           | 10,672  | 8192       | 83 MB  |
| Postgres (heap + PK index)     |  —      |  —         | 134 MB |

Heap sizes are close (78 MB vs 83 MB). Postgres is slightly larger because every row carries an MVCC tuple header (xmin, xmax, etc., ~23 bytes per row) which is the cost of full transactions. SQLite has a simpler row format so it packs tighter.

### Query performance

Same 3 queries, same data:

|                       | cold run  | warm run |
|-----------------------|-----------|----------|
| SQLite (mmap off)     | ~0.59 s   | ~0.24 s  |
| SQLite (mmap on)      | ~0.24 s   | ~0.24 s  |
| Postgres              | ~0.12 s   | ~0.11 s  |

Caveat: SQLite times include the `sqlite3` CLI process startup, while postgres times are just `\timing` ms inside an already-open `psql` session, so it's not a perfectly fair comparison. Still — postgres is clearly faster in the warm case because the server already has data in `shared_buffers` and doesn't pay any process-startup cost.

### mmap impact

The most interesting bit. Turning on mmap in SQLite made the **cold-cache run about 2.5x faster** (0.59 → 0.24 s) on the 78 MB DB. Once the file was already in OS cache, both modes were the same — both about 0.24 s.

Why: without mmap, SQLite calls `pread()` for each page it needs, which is a separate syscall per page. With mmap, the file is mapped into the process's address space and reads turn into ordinary memory accesses; the kernel handles paging and read-ahead. Once everything is resident, both paths are just RAM reads, so they look identical.

Postgres doesn't expose mmap because its design is different — it manages its own buffer pool. Bumping `shared_buffers` is the equivalent lever there.


## Commands I used

SQLite:

```
ls -lh imdb.db
sqlite3 imdb.db "PRAGMA page_size;"
sqlite3 imdb.db "PRAGMA page_count;"
sqlite3 imdb.db "PRAGMA mmap_size;"
sqlite3 imdb.db "PRAGMA cache_size;"
sqlite3 imdb.db "PRAGMA journal_mode;"

# without mmap
time sqlite3 imdb.db "PRAGMA mmap_size=0;
    SELECT COUNT(*) FROM ratings;
    SELECT AVG(averageRating), SUM(numVotes) FROM ratings;
    SELECT tconst, averageRating FROM ratings
      WHERE numVotes > 100000
      ORDER BY averageRating DESC LIMIT 5;"

# with mmap (256 MB)
time sqlite3 imdb.db "PRAGMA mmap_size=268435456; <same selects>"

ps aux | grep sqlite
```

PostgreSQL:

```
brew install postgresql@16
brew services start postgresql@16
createdb lab

psql -d lab
lab=# CREATE TABLE ratings (tconst TEXT PRIMARY KEY,
                            averageRating REAL,
                            numVotes INTEGER);
lab=# \copy ratings FROM 'title.ratings.tsv' WITH (FORMAT csv, DELIMITER E'\t', HEADER true);

lab=# SHOW block_size;
lab=# ANALYZE ratings;
lab=# SELECT relname, relpages, reltuples::bigint
       FROM pg_class WHERE relname='ratings';
lab=# SELECT pg_size_pretty(pg_relation_size('ratings'));
lab=# SELECT pg_size_pretty(pg_total_relation_size('ratings'));

lab=# \timing on
lab=# SELECT COUNT(*) FROM ratings;
lab=# SELECT AVG(averageRating), SUM(numVotes) FROM ratings;
lab=# SELECT tconst, averageRating FROM ratings
       WHERE numVotes > 100000 ORDER BY averageRating DESC LIMIT 5;

lab=# SHOW shared_buffers;
lab=# SHOW effective_cache_size;

ps aux | grep postgres
```


## What I learned

- A SQLite database file is exactly `page_size × page_count` bytes — no extra metadata file, the math actually checks out (`4096 × 19952 ≈ 78 MB`).
- mmap is **off** by default in SQLite. It mainly helps the cold-cache case; once the OS has the file cached, mmap and non-mmap paths are equivalent.
- Postgres uses 8 KB pages vs SQLite's 4 KB, so for the same data it has roughly half the page count. Both store about the same amount of heap data, but Postgres's MVCC headers add a small overhead per row.
- Postgres has no mmap knob; its in-server buffer pool (`shared_buffers`) plays the same role.
- Biggest practical difference is architectural: SQLite is a library inside *your* process, postgres is a separate server with multiple background processes. Different tools for different jobs — SQLite for embedded / single-process / "config files but smarter", postgres for anything multi-user or concurrent.
