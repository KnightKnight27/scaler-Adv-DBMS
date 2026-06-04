# Lab 2: SQLite3 and PostgreSQL Performance Exploration

**Student:** Talin Daga (24bcs10321)

## Objective
Explore internal storage and performance characteristics of SQLite3 and PostgreSQL — page structures, storage organisation, memory-mapped I/O, and query execution time comparison.

## Files

| File | Description |
|------|-------------|
| `sqlite3_exploration.py` | SQLite3 experiment (file size, pages, mmap, query timing, process info) |
| `postgres_exploration.py` | PostgreSQL experiment (block size, relation size, EXPLAIN ANALYZE, pg_stat) |

---

## Prerequisites

### SQLite3
Python 3 only — `sqlite3` is in the standard library, nothing to install.

### PostgreSQL

```bash
# macOS
brew install postgresql@14
brew services start postgresql@14

# Ubuntu / Debian
sudo apt install postgresql postgresql-contrib
sudo systemctl start postgresql

# Create a superuser (if needed)
sudo -u postgres psql -c "ALTER USER postgres PASSWORD 'postgres';"
```

### Python driver for PostgreSQL

```bash
pip install psycopg2-binary
```

---

## Running

### SQLite3 exploration
```bash
cd lab2
python3 sqlite3_exploration.py
```

### PostgreSQL exploration
1. Open `postgres_exploration.py` and edit `DB_PARAMS` at the top if your credentials differ.
2. Run:
```bash
cd lab2
python3 postgres_exploration.py
```

---

## Observations (fill in after running)

### SQLite3

| Metric | Observed Value |
|--------|---------------|
| Page size | |
| Page count after 5 000 rows | |
| File size after 5 000 rows | |
| Freelist count | |
| Journal mode | |
| mmap_size (default) | |
| Avg query time — mmap OFF | |
| Avg query time — mmap ON | |
| SQLite server processes found | |

### PostgreSQL

| Metric | Observed Value |
|--------|---------------|
| block_size | |
| pg_relation_size (5 000 rows) | |
| relpages | |
| reltuples | |
| Avg query time | |
| Active backend processes | |

---

## Comparison Summary

| Aspect | SQLite3 | PostgreSQL |
|--------|---------|------------|
| Architecture | Embedded (in-process) | Client-server |
| Storage format | Single `.db` file | Data directory, many files |
| Default page/block size | 4 096 bytes | 8 192 bytes |
| mmap support | `PRAGMA mmap_size` | OS-managed, not user-exposed |
| Concurrency model | File-level locking | MVCC (row-level) |
| Setup complexity | Zero (no server) | Server process required |
| Best for | Mobile, embedded, prototypes | Multi-user, web backends, enterprise |

---

## Analysis Questions

1. **What is the purpose of database pages?**

2. **How does SQLite store data differently from PostgreSQL?**

3. **What is memory-mapped I/O and why is it used?**

4. **How does mmap affect query performance?**

5. **Why does PostgreSQL use a client-server architecture?**

6. **What factors influence query execution time?**

7. **Which database is more suitable for embedded applications?**

8. **Which database is more suitable for large multi-user systems?**

9. **How do storage structures affect performance?**
