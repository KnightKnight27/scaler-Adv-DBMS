# Lab 4: SQLite3 Database Internal Structure Analysis Using XXD

**Student:** Talin Daga (24bcs10321)

## Objective
Explore the internal storage structure of a SQLite3 database file by examining raw bytes with `xxd`. Understand how SQLite organises the file header, B-tree pages, cell pointer arrays, record payloads, and schema metadata.

## Files

| File | Description |
|------|-------------|
| `sqlite3_hex_analysis.py` | Python script — creates the DB, runs all 8 analysis tasks |

> `lab4_analysis.db` is generated when you run the script and is intentionally excluded from version control.

## Prerequisites

| Tool | How to get it |
|------|--------------|
| Python 3 | Pre-installed on macOS/Linux |
| `xxd` | macOS: pre-installed. Ubuntu: `sudo apt install xxd` |
| `sqlite3` module | Part of Python standard library — nothing to install |

## Run

```bash
cd lab4
python3 sqlite3_hex_analysis.py
```

Each run recreates `lab4_analysis.db` from scratch, then prints all task output.

## Manual inspection after running

```bash
# Full hex dump (pipe to less to scroll)
xxd lab4_analysis.db | less

# First 100 bytes (database file header)
xxd -l 100 lab4_analysis.db

# B-tree page header of page 1 (bytes 100–119)
xxd -s 100 -l 20 lab4_analysis.db

# Open with the SQLite CLI
sqlite3 lab4_analysis.db
sqlite3> PRAGMA page_size;
sqlite3> SELECT * FROM sqlite_master;
```

---

## Tasks Demonstrated

| Task | Content |
|------|---------|
| 1 | Create DB with `students` table (10 rows, UNIQUE index on roll_no) |
| 2 | PRAGMA metadata: page_size, page_count, encoding, schema_version; sqlite_master |
| 3 | xxd of bytes 0–99 + annotated field-by-field breakdown of the 100-byte header |
| 4 | xxd of bytes 100–119 + annotated B-tree page header (type, cells, cell_content_start) |
| 5 | Cell pointer arrays decoded for page 1 (sqlite_master) and page 2 (students table) |
| 6 | Search raw file for text values; hex window shows UTF-8 records with varint headers |
| 7 | sqlite_master records; locate `CREATE TABLE` SQL verbatim in the raw hex |
| 8 | Full page map: 3 pages (schema root, table leaf, index leaf) with free-space stats |

---

## Observations (fill in after running)

### File Header (Task 3)

| Field | Bytes | Observed Value |
|-------|-------|---------------|
| Magic string | 0–15 | |
| Page size | 16–17 | |
| Write format version | 18 | |
| Text encoding | 56–59 | |
| Database size (pages) | 28–31 | |
| Schema cookie | 40–43 | |
| SQLite version | 96–99 | |

### B-Tree Page Header (Task 4)

| Field | Observed Value |
|-------|---------------|
| Page type byte | |
| Page type name | |
| Number of cells on page 1 | |
| Cell content area start | |
| Unallocated free space | |

### Cell Pointers (Task 5)

| Page | Cells | First pointer (hex) | First pointer (offset) |
|------|-------|---------------------|----------------------|
| 1 (sqlite_master) | | | |
| 2 (students table) | | | |

### File Layout (Task 8)

| Page | File bytes | Content | Cell count |
|------|-----------|---------|-----------|
| 1 | | | |
| 2 | | | |
| 3 | | | |

---

## Analysis Questions

1. **What is the purpose of the 100-byte SQLite file header?**

2. **What does the page type byte 0x0D indicate?**

3. **How does the cell pointer array allow O(1) record lookup?**

4. **In which direction does cell content grow within a page, and why?**

5. **Where is the `CREATE TABLE` SQL stored, and why does SQLite need it at startup?**

6. **Why does the `roll_no UNIQUE` constraint create an extra page in the file?**

7. **What is the difference between `page_size × page_count` and the actual file size?**

8. **How does SQLite's single-file architecture compare to PostgreSQL's data-directory model?**

---

## SQLite3 File Format Quick Reference

```
Offset   Size   Field
──────   ────   ─────────────────────────────────────────
 0       16     Magic: "SQLite format 3\0"
16        2     Page size  (big-endian; 1 → 65536)
18        1     Write format version  (1=legacy, 2=WAL)
19        1     Read format version
24        4     File change counter
28        4     Database size in pages
40        4     Schema cookie
44        4     Schema format number  (current = 4)
56        4     Text encoding  (1=UTF-8)
96        4     SQLite library version number

B-tree page header (at offset 100 for page 1, 0 for all others):
Offset   Size   Field
──────   ────   ─────────────────────────────────────────
 0        1     Page type  (0x0D=leaf table, 0x05=interior, ...)
 1        2     First freeblock offset  (0 = none)
 3        2     Number of cells
 5        2     Cell content area start offset
 7        1     Fragmented free bytes
[8        4     Right-most child page  (interior pages only)]
```
