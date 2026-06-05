# Lab 4: SQLite3 Database Internal Structure Analysis Using XXD

## Objective

To explore and analyze the internal storage structure of a SQLite3 database file using hexadecimal inspection tools — understanding how SQLite stores metadata, schema, B-tree pages, and records directly in the `.db` file.

---

## Tools Used

| Tool      | Purpose                                      |
|-----------|----------------------------------------------|
| `sqlite3` | Create and query the database                |
| `xxd`     | Hex dump the raw binary file                 |
| `strings` | Extract human-readable text from binary file |

### Install (if not already available)
```bash
sudo apt install sqlite3 xxd
```

---

## What the Script Does

### Task 1 — Database Creation
Creates `lab4.db` with a `students` table and inserts 5 records.

### Task 2 — Database Metadata
Uses SQLite `PRAGMA` commands to inspect page size, page count, root page, and table info.

### Task 3 — File Header Inspection
Reads the first 100 bytes using `xxd` and breaks down each field:

| Offset    | Field                  | Value              |
|-----------|------------------------|--------------------|
| 0–15      | Magic signature        | `SQLite format 3\0`|
| 16–17     | Page size              | e.g. `0x1000` = 4096 bytes |
| 18        | File format write ver  | `0x01`             |
| 19        | File format read ver   | `0x01`             |
| 28–31     | File change counter    | increments on write|

### Task 4 — B-Tree Page Analysis
Reads the page header at offset 100 (right after the file header):

| Offset | Field              | Meaning                        |
|--------|--------------------|--------------------------------|
| 100    | Page type          | `0x0d` = leaf table b-tree     |
| 101–102| First freeblock    | offset of first free block     |
| 103–104| Cell count         | number of records on this page |
| 105–106| Cell content offset| where cell content area starts |

### Task 5 — Cell Pointer Array
Cell pointers start at offset 108. Each is a 2-byte offset pointing to a record within the page. SQLite uses these to locate records without scanning the full page.

### Task 6 — Record Storage
Hex dumps the last 256 bytes where actual row data is stored. Uses `strings` to show human-readable values (names, course names, etc.) embedded in the binary file.

### Task 7 — Schema Storage
Shows that `CREATE TABLE students (...)` is stored as plain text inside `sqlite_master` — which itself lives in the database file. No separate catalog file needed.

### Task 8 — Physical File Layout
Summarizes the complete layout:
```
[Page 1: offset 0]
  Bytes 0–99    → SQLite file header (100 bytes)
  Bytes 100+    → B-tree page header + cell pointers + records
[Page 2+: offset 4096, 8192, ...]
  → additional data/overflow pages
```

---

## How to Run

```bash
chmod +x lab4_sqlite_analysis.sh
./lab4_sqlite_analysis.sh
```

---

## Sample Output (excerpts)

```
>>> TASK 1: Database Creation
1|Alice|20|DBMS
2|Bob|22|OS
...
File size: 8192 bytes

>>> TASK 3: SQLite File Header
  Bytes 00-15 : File signature  -> 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
  Bytes 16-17 : Page size       -> 0x1000 = 4096 bytes
  Byte  100   : Page type       -> 0x0d (leaf table b-tree page)
  Bytes 103-104: Cell count     -> 0x0006 = 6 cells

>>> TASK 7: Schema Storage
  Searching for CREATE TABLE in raw bytes:
  CREATE TABLE students (
```

---

## Observations

- The first 16 bytes of every `.db` file are the ASCII string `SQLite format 3` — this is how tools identify SQLite files.
- Page size is stored at offset 16–17 as a big-endian 2-byte integer. Default is 4096 bytes.
- Byte 100 is `0x0d` meaning a **leaf table b-tree page** — this is where actual row data lives.
- Cell count at bytes 103–104 includes both user rows AND the schema row in `sqlite_master`.
- `CREATE TABLE` SQL is stored verbatim as text inside the file — SQLite is its own catalog.
- `strings lab4.db` reveals all the names and course values stored as raw text in the binary.

---

## Key Takeaways

- SQLite stores everything — header, schema, and data — in a **single file**.
- B-tree pages organize records efficiently with cell pointer arrays for O(log n) lookup.
- The file header is always exactly 100 bytes at the start of the file.
- Understanding the physical layout explains why SQLite is fast for reads but has limited write concurrency (whole-file locking).

---

## Author
Submitted as part of Lab 4 – Database Systems Lab  
Date: May 2026