# Lab: SQLite3 vs PostgreSQL

Role Number: 24BCS10123
Name: Kushal Talati

I did this on my Mac (M-series, macOS Darwin 25.3.0).
SQLite version was 3.51.0 and Postgres was 16.13 (installed via brew).

For the data I didn't want to make up a fake table, so I grabbed the IMDb `title.ratings.tsv` from their public datasets page (https://datasets.imdbws.com/). It's real movie ratings data, around 1.67 million rows, 3 columns (tconst, averageRating, numVotes). Big enough that the query timings are not noise.

```
curl -L -O https://datasets.imdbws.com/title.ratings.tsv.gz
gunzip title.ratings.tsv.gz
wc -l title.ratings.tsv
# 1667512   (incl header)
```


## 1. SQLite3 Exploration

### Setting it up

SQLite was already on the Mac (`/usr/bin/sqlite3`). I loaded the tsv into a fresh DB:

```
sqlite3 imdb.db
> CREATE TABLE ratings (tconst TEXT PRIMARY KEY, averageRating REAL, numVotes INTEGER);
> .mode tabs
> .import --skip 1 title.ratings.tsv ratings
> SELECT COUNT(*) FROM ratings;
1667511
```

### File size with `ls -lh`

```
$ ls -lh imdb.db
-rw-r--r--  1 kushaltalati  staff    78M  May  9 12:58 imdb.db
```

So the DB came out to 78 MB.

### PRAGMA commands

```
sqlite> PRAGMA page_size;       -> 4096
sqlite> PRAGMA page_count;      -> 19952
sqlite> PRAGMA freelist_count;  -> 0
sqlite> PRAGMA mmap_size;       -> 0
sqlite> PRAGMA cache_size;      -> 2000
sqlite> PRAGMA journal_mode;    -> delete
```

Quick sanity check: 4096 * 19952 = ~78 MB, matches the file size. So the SQLite file really is just `page_size * page_count` bytes.

Also `mmap_size` is 0 by default. So SQLite is not using memory-mapping unless you turn it on.

### Playing with mmap_size

The way I tested this was just running the same 3 queries (count, an aggregate, and a filter+sort+limit) three times in a row and timing them with the shell `time` builtin.

Without mmap (`PRAGMA mmap_size=0`):

```
$ time sqlite3 imdb.db "PRAGMA mmap_size=0;
                        SELECT COUNT(*) FROM ratings;
                        SELECT AVG(averageRating), SUM(numVotes) FROM ratings;
                        SELECT tconst, averageRating FROM ratings
                          WHERE numVotes > 100000
                          ORDER BY averageRating DESC LIMIT 5;"
```

| run | real time |
|-----|-----------|
| 1   | 0.40 s    |
| 2   | 0.20 s    |
| 3   | 0.20 s    |

With mmap (`PRAGMA mmap_size=268435456`, i.e. 256 MB):

| run | real time |
|-----|-----------|
| 1   | 0.20 s    |
| 2   | 0.22 s    |
| 3   | 0.20 s    |

First run without mmap is the slow one (0.40s). After that it speeds up because the OS already has the file in cache. With mmap on, even the first run is fast. Once things are hot, both modes are pretty much the same. So mmap mostly helps the cold case.

### `ps aux | grep sqlite`

While a query was running:

```
$ ps aux | grep sqlite | grep -v grep
kushaltalati 30360 20.2 0.1 435310416 11472 ?? RN  1:01PM 0:00.14
  sqlite3 imdb.db PRAGMA mmap_size=268435456; SELECT * FROM ratings LIMIT 1000000;
```

VSZ looks huge (~435 GB) but that's just how macOS reports virtual address space, not real memory use. RSS was only ~11 MB. Tried again with mmap=0 and RSS was basically the same, since the rows are being streamed out one by one.


## 2. PostgreSQL Setup

### Installing

I installed it with Homebrew:

```
brew install postgresql@16
brew services start postgresql@16
createdb lab
```

`psql -d lab -c "SELECT version();"` returned PostgreSQL 16.13 (Homebrew).

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

Postgres calls them blocks but it's the same idea. Default is 8 KB (and it's a compile-time setting, you can't change it on a running cluster, which I didn't know).

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

So 10,672 pages * 8 KB = ~83 MB heap. Confirmed with:

```
lab=# SELECT pg_size_pretty(pg_relation_size('ratings'))       AS table_size,
             pg_size_pretty(pg_total_relation_size('ratings')) AS total_size;
 table_size | total_size
------------+------------
 83 MB      | 134 MB
```

134 MB includes the primary key index. Found the actual file too:

```
$ psql -d lab -At -c "SELECT pg_relation_filepath('ratings');"
base/16384/16385

$ ls -lh /opt/homebrew/var/postgresql@16/base/16384/16385*
-rw-------  ...  83M  16385
-rw-------  ...  40K  16385_fsm
```

### Query execution time

Used `\timing on` in psql and ran the same three queries 3 times.

```
lab=# \timing on
lab=# SELECT COUNT(*) FROM ratings;
lab=# SELECT AVG(averageRating), SUM(numVotes) FROM ratings;
lab=# SELECT tconst, averageRating FROM ratings
        WHERE numVotes > 100000 ORDER BY averageRating DESC LIMIT 5;
```

| run | COUNT(*) | AVG/SUM | filtered top-5 |
|-----|----------|---------|----------------|
| 1   | 30.9 ms  | 49.8 ms | 43.6 ms        |
| 2   | 36.8 ms  | 50.3 ms | 38.8 ms        |
| 3   | 27.1 ms  | 46.5 ms | 37.2 ms        |

3 queries together finish in around 110-130 ms in postgres, and there isn't a big cold/warm gap like SQLite, probably because the server is already running and has the buffer pool warm.

### `ps aux | grep postgres`

```
postgres -D /opt/homebrew/var/postgresql@16
postgres: checkpointer
postgres: background writer
postgres: walwriter
postgres: autovacuum launcher
postgres: logical replication launcher
```

Big difference from SQLite — postgres is a real multi-process server (a parent + helpers + a backend per connection). SQLite has no server, it's just a library inside whatever process opens the file.

Postgres doesn't have `mmap_size`. Closest equivalent is `shared_buffers`:

```
lab=# SHOW shared_buffers;        -- 128MB
lab=# SHOW effective_cache_size;  -- 4GB
```

So instead of mmap-ing, postgres reads the file through the OS but keeps copies of hot pages in its own 128 MB pool.


## 3. Comparison: SQLite3 vs PostgreSQL

### Page Size

| | SQLite | Postgres |
|--|--------|----------|
| default | 4 KB (4096 B) | 8 KB (8192 B) |
| changeable? | yes, `PRAGMA page_size = N` before DB creation | no, compile-time only |

Postgres uses a bigger page so each I/O reads twice as much data at once. SQLite's smaller page is fine for embedded / small DB use cases.

### Page Count

For the same 1.67M rows:

| | pages | bytes/page | total |
|--|-------|------------|-------|
| SQLite  | 19,952 | 4096 | 78 MB |
| Postgres (heap only) | 10,672 | 8192 | 83 MB |
| Postgres (heap + PK index) | - | - | 134 MB |

Almost the same on disk for the heap. Postgres takes a bit more because every row carries an MVCC tuple header (xmin/xmax etc.) — cost of full transaction support.

### Query Performance

Same 3 queries, same data:

| | cold run | warm run |
|--|----------|----------|
| SQLite (mmap off) | ~0.40 s | ~0.20 s |
| SQLite (mmap on)  | ~0.20 s | ~0.20 s |
| Postgres          | ~0.12 s | ~0.11 s |

One thing — the SQLite times include the `sqlite3` CLI process startup, while postgres times are summed `\timing` ms from an already-open psql session. So it's not a perfectly fair comparison. Even so, postgres is clearly faster in the warm case because the server already has data in `shared_buffers`.

### mmap impact

This was the most interesting part. Turning on mmap in SQLite made the cold-cache run about 2x faster (0.40 -> 0.20 s) on the 78 MB DB. Once the file was already in OS cache it didn't really matter anymore — both modes around 0.20 s.

My understanding of why: without mmap, SQLite does a `pread()` syscall for every page, which is a lot of small syscalls. With mmap the file is mapped into the process and reads become memory access; the kernel handles paging and read-ahead. Once everything is resident in RAM both paths are just RAM reads, so they look the same.

Postgres doesn't have an mmap knob because its design is different — it manages its own buffer pool. Bumping `shared_buffers` is the equivalent lever there.


## Commands I used

SQLite:

```
ls -lh imdb.db
sqlite3 imdb.db "PRAGMA page_size;"
sqlite3 imdb.db "PRAGMA page_count;"
sqlite3 imdb.db "PRAGMA mmap_size;"
sqlite3 imdb.db "PRAGMA cache_size;"
sqlite3 imdb.db "PRAGMA journal_mode;"

# timing without mmap
time sqlite3 imdb.db "PRAGMA mmap_size=0;
                      SELECT COUNT(*) FROM ratings;
                      SELECT AVG(averageRating), SUM(numVotes) FROM ratings;
                      SELECT tconst, averageRating FROM ratings
                        WHERE numVotes > 100000
                        ORDER BY averageRating DESC LIMIT 5;"

# timing with mmap (256 MB)
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

- A SQLite DB file is just `page_size * page_count` bytes, the math actually checks out.
- mmap is off in SQLite by default. It helps for cold reads but doesn't change much once data is in OS cache.
- Postgres uses 8 KB pages vs SQLite's 4 KB, so about half the page count for the same data.
- Postgres doesn't expose mmap. Its `shared_buffers` is roughly the same idea but lives inside the server itself.
- Biggest practical difference is that SQLite is just a library inside your own process and postgres is a separate server. Different tools for different jobs.
