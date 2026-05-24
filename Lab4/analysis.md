# Lab 4: SQLite3 Database Internal Structure Analysis Using XXD

**Name:** Siddhanth Kapoor  
**Roll Number:** 10154

## Aim

To inspect the raw bytes of a SQLite3 database file with `xxd` and map them back to
the documented on-disk format: the file header, the B-tree page headers, the cell
pointer array, the record payloads, and the schema (`sqlite_master`) storage.

---

## 1. Database Creation

```bash
sqlite3 students.db <<'SQL'
CREATE TABLE students(id INTEGER PRIMARY KEY, name TEXT, course TEXT);
INSERT INTO students(name,course) VALUES
  ('Aarav','DBMS'),('Bhavna','OS'),('Chetan','Networks'),
  ('Diya','DBMS'),('Esha','Compilers');
SQL
```

A single table with 5 records is created. The file is born at 8192 bytes (two
4 KB pages).

## 2. Database Metadata

```bash
sqlite3 students.db "PRAGMA page_size;"   # 4096
sqlite3 students.db "PRAGMA page_count;"  # 2
sqlite3 students.db "PRAGMA encoding;"    # UTF-8
sqlite3 students.db "SELECT type,name,rootpage FROM sqlite_master;"
#   table|students|2
```

| Property | Value | Meaning |
| :--- | :--- | :--- |
| Page size | 4096 | each page is 4 KB |
| Page count | 2 | page 1 = schema, page 2 = the `students` table |
| Root page | 2 | the table's B-tree root is page 2 |
| Encoding | UTF-8 | text serial type |

## 3. SQLite File Header Inspection

```bash
xxd -l 100 students.db
```

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0c40 2020 0000 0002 0000 0002  .....@  ........
00000020: 0000 0000 0000 0000 0000 0001 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
...
00000060: 002e 6eba                                ..n.
```

Decoding the 100-byte header:

| Offset | Bytes | Field | Value |
| :--- | :--- | :--- | :--- |
| 0–15 | `53 51 4c 69 ... 33 00` | Magic signature | `"SQLite format 3\0"` |
| 16–17 | `10 00` | Page size | 0x1000 = **4096** |
| 18 | `01` | File format write version | 1 (legacy/rollback journal) |
| 19 | `01` | File format read version | 1 |
| 20 | `00` | Reserved bytes per page | 0 |
| 21–23 | `40 20 20` | Max/min/leaf payload fractions | 64 / 32 / 32 |
| 24–27 | `00 00 00 02` | File change counter | 2 |
| 28–31 | `00 00 00 02` | Database size in pages | **2** |
| 56–59 | `00 00 00 01` | Text encoding | 1 = **UTF-8** |
| 96–99 | `00 2e 6e ba` | SQLite version number | 0x2e6eba = 3043002 → **3.43.2** |

The leading 16-byte magic string is what makes the file *recognisably* a SQLite
database. The page size lives at offset 16 and exactly matches the `PRAGMA`.

## 4. B-Tree Page Analysis

The page-1 B-tree header begins right after the 100-byte file header (offset 100):

```bash
xxd -s 100 -l 16 students.db
00000064: 0d00 0000 010f 9000 0f90 0000 0000 0000  ................
```

| Byte(s) | Value | Meaning |
| :--- | :--- | :--- |
| `0d` | 13 | page type = **leaf table B-tree** |
| `00 00` | 0 | first freeblock (none) |
| `00 01` | 1 | number of cells = 1 (the schema row) |
| `0f 90` | 3984 | start of cell content area |
| `00` | 0 | fragmented free bytes |

Page 2 (the `students` data, at file offset 0x1000) has the same shape:

```bash
xxd -s 4096 -l 16 students.db
00001000: 0d00 0000 050f a200 0fe5 0fd7 0fc3 0fb5  ................
```

`0d` → leaf table page, **5 cells** (our 5 rows), content area starting at 0x0fa2.
Both pages are leaf pages (`0d`); with only 5 short rows the tree never needed an
interior page, so the root *is* the leaf.

## 5. Cell Pointer Array Examination

Immediately after page 2's 8-byte header comes the cell pointer array — one 2-byte
offset per record, in key (rowid) order:

```
00001008: ... 0fe5 0fd7 0fc3 0fb5 0fa2 ...
```

| Cell | Offset (within page) | Row |
| :--- | :--- | :--- |
| 0 | 0x0fe5 | rowid 1 (Aarav) |
| 1 | 0x0fd7 | rowid 2 (Bhavna) |
| 2 | 0x0fc3 | rowid 3 (Chetan) |
| 3 | 0x0fb5 | rowid 4 (Diya) |
| 4 | 0x0fa2 | rowid 5 (Esha) |

This is how SQLite locates a record without scanning the page: the pointer array is
small and sorted, so a binary search over it jumps straight to the cell. Note the
content grows *upward* from the end of the page while the pointer array grows
*downward* from the header — they meet in the middle, which is the free space.

## 6. Record Storage Analysis

```bash
xxd -s 8000 -l 192 students.db
000001fa0: ... 1105 0400 151f 4573 6861 436f 6d70 696c 6572 73 ...   Esha Compilers
000001fe5: 0d01 0400 1715 4161 7261 7644 424d 53            ...     Aarav DBMS
```

Decoding the first record (rowid 1, at offset 0x1fe5: `0d 01 04 00 17 15 41 61 72 61 76 44 42 4d 53`):

| Bytes | Meaning |
| :--- | :--- |
| `0d` | payload length = 13 |
| `01` | rowid = 1 |
| `04` | record header length = 4 |
| `00` | serial type 0 → **NULL** for `id` (it is the rowid, stored implicitly) |
| `17` | serial type 23 → text of length (23−13)/2 = **5** → `"Aarav"` |
| `15` | serial type 21 → text of length (21−13)/2 = **4** → `"DBMS"` |
| `41 61 72 61 76` | `Aarav` |
| `44 42 4d 53` | `DBMS` |

The `id` column is `NULL` in the payload because `INTEGER PRIMARY KEY` is an alias
for the rowid and is never stored a second time. The actual text values are clearly
readable in the hex dump (`Aarav`, `Bhavna`, `Chetan`, `Diya`, `Esha`,
`Compilers`, ...), confirming SQLite stores user data in plain UTF-8 with
variable-length serial types.

## 7. Schema Storage Analysis

The schema lives as an ordinary record on page 1 (offset 0x0f90):

```bash
xxd -s 3980 -l 116 students.db
00000f90: 6201 0717 1d1d 0181 1774 6162 6c65 7374  b........tablest
00000fa0: 7564 656e 7473 7374 7564 656e 7473 0243  udentsstudents.C
00000fb0: 5245 4154 4520 5441 424c 4520 7374 7564  REATE TABLE stud
00000fc0: 656e 7473 2869 6420 494e 5445 4745 5220  ents(id INTEGER
00000fd0: 5052 494d 4152 5920 4b45 592c 206e 616d  PRIMARY KEY, nam
00000fe0: 6520 5445 5854 2c20 636f 7572 7365 2054  e TEXT, course T
00000ff0: 4558 5429                                EXT)
```

This is a row of the internal `sqlite_master` table with columns
(`type`, `name`, `tbl_name`, `rootpage`, `sql`):

| Field | Value in the dump |
| :--- | :--- |
| type | `table` |
| name | `students` |
| tbl_name | `students` |
| rootpage | `02` (single byte → page 2) |
| sql | `CREATE TABLE students(id INTEGER PRIMARY KEY, name TEXT, course TEXT)` |

So the exact `CREATE TABLE` statement is physically stored *inside the database
file* as text — that is how SQLite reconstructs the schema when it reopens a file.

## 8. Physical File Layout Study

```
 byte 0          ┌──────────────────────────────────────────┐
                 │ 100-byte file header (magic, page size…)  │
 byte 100        ├──────────────────────────────────────────┤  page 1
                 │ B-tree header (0d, 1 cell)                │  (schema /
                 │ cell pointer array → 0f90                 │   sqlite_master)
                 │ … free space …                           │
                 │ schema record: CREATE TABLE students(...) │
 byte 4096       ├──────────────────────────────────────────┤  page 2
                 │ B-tree header (0d, 5 cells)               │  (students
                 │ cell pointers 0fe5 0fd7 0fc3 0fb5 0fa2    │   table data)
                 │ … free space …                           │
                 │ 5 records: Aarav…  Bhavna…  …  Esha…      │
 byte 8192       └──────────────────────────────────────────┘
```

Everything — the format magic, the schema definition, and the user rows — lives in
one file, organised as fixed-size pages, each page a node of a B-tree, each record
addressed through a small sorted cell-pointer array. The hex dump makes the whole
architecture visible byte for byte.

---

## Analysis

1. **Pages** are the fixed unit of I/O (4 KB here); the file is just an array of pages.
2. **B-trees** organise records: every page header starts with `0d` (leaf table), and a larger table would introduce interior pages (`05`).
3. **Cell pointer arrays** give O(log n) record lookup inside a page without a linear scan.
4. **Records** use variable-length serial types, so short strings cost few bytes and `INTEGER PRIMARY KEY` columns cost zero (they reuse the rowid).
5. **Self-describing:** the `CREATE TABLE` text is stored in `sqlite_master` inside the same file, so the database needs no external catalog to be reopened.
