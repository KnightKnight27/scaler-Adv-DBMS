# Lab 4 — SQLite3 Internal Structure Analysis Using `xxd`

Explore how SQLite physically lays out a database on disk: the file
header, B-tree pages, cell pointer arrays, record payloads, and the
schema catalog (`sqlite_master`). Everything below is observed directly
from `students.db` using `xxd`.

## How to run

```bash
chmod +x setup.sh analyze.sh run_all.sh
./run_all.sh
```

`setup.sh` creates `students.db` and captures `PRAGMA`/`sqlite_master`
output into `results/metadata.txt`. `analyze.sh` slices the binary file
into the regions referenced by each task and writes them to
`results/*.hex`.

Requires `sqlite3` and `xxd`. On Ubuntu/WSL:

```bash
sudo apt install sqlite3 xxd
```

## Files

| Path | Purpose |
|---|---|
| `setup.sh`    | Creates DB, inserts 10 rows, dumps metadata |
| `analyze.sh`  | Runs `xxd` over the file at the right offsets |
| `run_all.sh`  | Convenience wrapper |
| `students.db` | The SQLite database under inspection |
| `results/metadata.txt`         | Output of Task 2 |
| `results/header.hex`           | First 100 bytes (Task 3) |
| `results/page1.hex`            | Page 1 — header + `sqlite_master` (Tasks 4, 5, 7) |
| `results/page2.hex`            | Page 2 — `students` table root (Tasks 4, 5, 6) |
| `results/page1_btree_header.hex` | 8-byte B-tree header for page 1 (Task 4) |
| `results/page2_btree_header.hex` | 8-byte B-tree header for page 2 (Task 4) |
| `results/full_dump.hex`        | Full file dump (Task 8) |

---

## Task 1 — Database Creation

The `students` table:

```sql
CREATE TABLE students (
    id    INTEGER PRIMARY KEY,
    name  TEXT NOT NULL,
    grade TEXT,
    marks INTEGER
);
```

Ten rows are inserted (`Alice`, `Bob`, `Charlie`, `Diana`, `Ethan`,
`Fiona`, `George`, `Hannah`, `Ivan`, `Julia`). The file `students.db` is
created in the current directory.

## Task 2 — Database Metadata

Captured in `results/metadata.txt`. Key values (default SQLite settings):

- **Page size**: `4096` bytes
- **Page count**: `2` (page 1 = header + schema, page 2 = table root)
- **Root page** of `students`: `2` (from `sqlite_master.rootpage`)
- **File size**: `8192` bytes (page_size × page_count)

The `sqlite_master` table is SQLite's internal catalog — it is itself a
regular B-tree, stored starting just after the 100-byte file header on
page 1.

## Task 3 — SQLite File Header

`results/header.hex` shows the first 100 bytes. Annotated layout:

| Offset | Length | Field | Expected value |
|---|---|---|---|
| 0  | 16 | Magic string         | `SQLite format 3\0` (ASCII: `5371 6c69 7465 2066 6f72 6d61 7420 3300`) |
| 16 | 2  | Page size            | `0x1000` = 4096 (big-endian) |
| 18 | 1  | Write format version | `0x01` (legacy rollback journal) |
| 19 | 1  | Read format version  | `0x01` |
| 20 | 1  | Reserved space / page | `0x00` |
| 21 | 1  | Max embedded payload fraction | `0x40` (64) |
| 22 | 1  | Min embedded payload fraction | `0x20` (32) |
| 23 | 1  | Leaf payload fraction         | `0x20` (32) |
| 24 | 4  | File change counter   | varies — increments on write |
| 28 | 4  | Database size (pages) | `0x00000002` = 2 |
| 32 | 4  | First freelist page   | `0x00000000` (no free pages) |
| 36 | 4  | Number of freelist pages | `0x00000000` |
| 40 | 4  | Schema cookie         | varies |
| 44 | 4  | Schema format number  | `0x00000004` (format 4 — modern) |
| 48 | 4  | Default page cache size | `0x00000000` |
| 52 | 4  | Largest root b-tree page (vacuum) | `0x00000000` |
| 56 | 4  | Text encoding         | `0x00000001` = UTF-8 |
| 60 | 4  | User version          | `0x00000000` |
| 64 | 4  | Incremental vacuum    | `0x00000000` |
| 68 | 4  | Application ID        | `0x00000000` |
| 72 | 20 | Reserved              | zeros |
| 92 | 4  | Version-valid-for     | matches file change counter |
| 96 | 4  | SQLite version number | e.g. `0x002e5c00` for 3.39.x |

The presence of the magic string at offset 0 is what makes the file a
"valid SQLite database" — `file students.db` reports it based on those
16 bytes.

## Task 4 — B-Tree Page Analysis

Every page after the file header begins with an 8-byte (leaf) or 12-byte
(interior) B-tree page header. On page 1 this header starts at offset
**100** (right after the file header); on every other page it starts at
offset 0 of the page.

8-byte leaf page header:

| Offset | Len | Field |
|---|---|---|
| 0 | 1 | Page type — `0x0D` table-leaf, `0x05` table-interior, `0x0A` index-leaf, `0x02` index-interior |
| 1 | 2 | First freeblock offset (`0x0000` if none) |
| 3 | 2 | Number of cells on page |
| 5 | 2 | Cell content area start offset |
| 7 | 1 | Number of fragmented free bytes |

Interior pages have 4 extra bytes (right-child pointer).

In `results/page2_btree_header.hex` for page 2 (`students` table root):

- Byte 0 = `0x0D` → table B-tree **leaf**
- Bytes 3–4 → number of cells = 10 (one per inserted row)
- Bytes 5–6 → cell content area start — somewhere near the end of the
  page (cells grow upward from the end; pointers grow downward from the
  header)
- "Free space" is the gap between the end of the cell pointer array and
  the start of the cell content area.

In `results/page1_btree_header.hex` (the `sqlite_master` leaf):

- Byte 0 = `0x0D` → also a table B-tree leaf
- Bytes 3–4 → number of cells = **1** (just the `students` table entry)

## Task 5 — Cell Pointer Array

Right after the page header sits an array of 2-byte big-endian offsets,
one per cell, pointing into the cell content area at the bottom of the
page. The N-th offset is the byte position of the N-th record within
the same page.

For page 2 with 10 cells, bytes 108–127 of the file (page header ends
at byte 108 for page 1; on page 2 it ends at offset 8 within the page)
contain ten 2-byte offsets. Each offset, when read, jumps directly to
that record — no scanning required. This is what makes "row at offset
X" lookups O(1) within a page.

Locate them in `results/page2.hex`: the row after the type/cell-count
bytes is all cell pointers until you hit the run of `00` (free space).

## Task 6 — Record Storage Analysis

A single cell in a table-leaf page has the structure:

```
[ payload-length varint ][ rowid varint ][ record header ][ record body ]
```

The record header is itself: `[ header-length varint ][ serial-type varints... ]`

Serial types encode column type and size:

| Serial type | Meaning |
|---|---|
| 0  | NULL |
| 1  | 8-bit int |
| 2  | 16-bit int |
| 3  | 24-bit int |
| 4  | 32-bit int |
| 5  | 48-bit int |
| 6  | 64-bit int |
| 8  | constant 0 |
| 9  | constant 1 |
| ≥13 odd  | TEXT of length `(n-13)/2` |
| ≥12 even | BLOB of length `(n-12)/2` |

For a row like `(1, 'Alice', 'A', 92)`:

- Column `id` is the `INTEGER PRIMARY KEY`, so its value lives in the
  rowid varint (not in the body) and the serial type for `id` is 0
  (NULL placeholder).
- `'Alice'` (5 chars) → serial type `13 + 2*5 = 23` = `0x17`
- `'A'`   (1 char)  → serial type `13 + 2*1 = 15` = `0x0F`
- `92` fits in 1 byte → serial type `1`

Searching `results/page2.hex` for ASCII text reveals all ten names
(`Alice`, `Bob`, …, `Julia`) packed near the **end** of the page —
SQLite grows cell content from the bottom of the page upward.

## Task 7 — Schema Storage

`sqlite_master` is stored as records on **page 1**, right after the file
header and B-tree page header. Each row has columns:
`(type, name, tbl_name, rootpage, sql)`.

Open `results/page1.hex` and scan for the ASCII text — you will see
literally:

```
table students students <rootpage> CREATE TABLE students(id INTEGER PRIMARY KEY, name TEXT NOT NULL, grade TEXT, marks INTEGER)
```

embedded near the end of page 1. This proves the `CREATE TABLE`
statement is physically stored inside the database file as part of the
catalog — that's how SQLite knows the schema on the next open without
needing any external metadata.

## Task 8 — Physical File Layout

Total file = `page_size × page_count = 4096 × 2 = 8192` bytes:

```
Offset   Range          Content
-------  -------------  ----------------------------------------------------
0        0..99          SQLite file header (100 bytes)
100      100..107       B-tree page header for sqlite_master (page 1 leaf)
108      108..end-of-p1 sqlite_master cell pointer array + free space
                        + (near bottom of page 1) the schema record for
                        the students table — including the CREATE TABLE SQL
4096     4096..4103     B-tree page header for students (page 2 leaf)
4104     ...            Ten 2-byte cell pointers
...      ...            Free space
...      ...end-of-p2   Ten student records (Alice..Julia) packed from
                        the bottom of page 2 upward
```

Key takeaways:

- One file holds **both** user data and metadata.
- Everything is organised as B-trees: the schema catalog is one B-tree,
  every user table is another.
- Inside each page, **cells grow from the bottom up** and **pointers
  grow from the top down** — a classic "slotted page" layout used by
  most disk-oriented DBMSs (PostgreSQL, Oracle, etc.).
- Variable-length records are addressed via the cell pointer array, so
  rows can be different sizes without harming lookup cost.

## References

- SQLite file format spec: <https://www.sqlite.org/fileformat.html>
- `PRAGMA` documentation: <https://www.sqlite.org/pragma.html>
- Record format & varints: <https://www.sqlite.org/fileformat2.html#record_format>
