# Lab 4 — SQLite On-Disk Format Walkthrough

**Name.** Aman Yadav
**Roll No.** 24BCS10183
**Class.** B (2nd year)

A byte-level walkthrough of how SQLite stores a small `students` table on
disk. The DB has 5 rows and one auto-created UNIQUE index on `email`. After
`VACUUM` the file is exactly **3 pages × 4096 B = 12 288 B**.

## Files

| File | Purpose |
|---|---|
| `students.sql` | DDL + 5 inserts + `VACUUM` |
| `students.db`  | 12 288 B SQLite database |
| `students.hex` | full `xxd -g 1 -c 16` dump |
| `README.md`    | this walkthrough |

## Reproduce

```bash
cd Lab4/24BCS10183-aman-yadav
rm -f students.db
sqlite3 students.db < students.sql
xxd -g 1 -c 16 students.db > students.hex
```

## How SQLite lays out the file

SQLite splits the file into fixed-size pages (4096 B here). Every page is one
of: leaf table (`0x0D`), interior table (`0x05`), leaf index (`0x0A`), or
interior index (`0x02`). Our DB has 3 pages:

```
0x0000  page 1  → sqlite_master (schema catalog)             leaf table (0x0D)
0x1000  page 2  → students rows                              leaf table (0x0D)
0x2000  page 3  → sqlite_autoindex_students_1 (UNIQUE email) leaf index (0x0A)
```

---

## Page 1 — the 100-byte file header (0x0000)

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 0c 40 20 20 00 00 00 03 00 00 00 03  .....@  ........
```

Decoding the header bytes I rely on:

| Offset | Bytes | Meaning |
|---|---|---|
| 0x0000 | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | magic string "SQLite format 3\0" |
| 0x0010 | `10 00` | page size = `0x1000` = **4096** |
| 0x0012 | `01 01` | file format write = 1, read = 1 |
| 0x0018 | `00 00 00 03` | file change counter = 3 |
| 0x001C | `00 00 00 03` | in-header database size = **3 pages** |
| 0x002C | `00 00 00 02` | schema cookie = 2 (incremented per schema change) |

The fixed file header is exactly 100 bytes. Page 1's B-tree header starts
immediately after it, at **0x0064**.

---

## Page 1 — the schema B-tree (sqlite_master)

```
00000060: 00 2e 8d f8 0d 0f ec 00 02 0e 8a 00 0e 8a 0f bb  ................
```

Skipping the last 4 bytes of the file header (`00 2e 8d f8` — sqlite version),
the page-1 B-tree header begins at 0x0064:

| Offset | Bytes | Meaning |
|---|---|---|
| 0x0064 | `0D` | page type = leaf table |
| 0x0065 | `0F EC` | first freeblock = 0x0FEC |
| 0x0067 | `00 02` | **2 cells** on this page |
| 0x0069 | `0E 8A` | cell content area starts at 0x0E8A |
| 0x006B | `00` | fragmented free bytes |

The 2 cell pointers follow at **0x006C**:

```
0x006C: 0E 8A   → students table schema row    @ file offset 0x0E8A
0x006E: 0F BB   → sqlite_autoindex_1 row       @ file offset 0x0FBB
```

### students table schema cell @ 0x0E8A

```
00000e80: 00 00 00 00 00 00 00 00 00 00 82 2e 01 07 17 1d
00000e90: 1d 01 84 2f 74 61 62 6c 65 73 74 75 64 65 6e 74  .../tablestudent
00000ea0: 73 73 74 75 64 65 6e 74 73 02 43 52 45 41 54 45  sstudents.CREATE
00000eb0: 20 54 41 42 4c 45 20 73 74 75 64 65 6e 74 73 20   TABLE students 
...
00000fb0: 54 49 4d 45 53 54 41 4d 50 0a 29                 TIMESTAMP.)
```

You can literally read `tablestudentsstudents` followed by the full
`CREATE TABLE students (...)` SQL. The record layout is:

- `82 2e` → payload size (varint, decodes to 302 bytes)
- `01`    → rowid 1
- `07`    → header length 7
- `17 1d 1d 01 84 2f` → serial-type codes for the 5 columns
  - `0x17` text(5)  → "table"
  - `0x1D` text(8)  → "students"
  - `0x1D` text(8)  → "students" (tbl_name)
  - `0x01` int1     → rootpage = 2  ← **the students rows live on page 2**
  - `0x84 2F` text  → the CREATE TABLE source

### autoindex schema cell @ 0x0FBB

```
00000fb0: ............................. 2f 02 06 17 43
00000fc0: 1d 01 00 69 6e 64 65 78 73 71 6c 69 74 65 5f 61  ...indexsqlite_a
00000fd0: 75 74 6f 69 6e 64 65 78 5f 73 74 75 64 65 6e 74  utoindex_student
00000fe0: 73 5f 31 73 74 75 64 65 6e 74 73 03              s_1students.
```

- `2F` → payload size 47
- `02` → rowid 2
- `06 17 43 1d 01 00` → header: text(5)="index", text(27)="sqlite_autoindex_students_1", text(8)="students", int1=3 (rootpage), NULL (no SQL)
- `03` → **rootpage 3** ← the index lives on page 3

---

## Page 2 — the actual students rows (0x1000)

```
00001000: 0d 00 00 00 05 0e 9d 00 0f ad 0f 66 0f 22 0e de  ...........f."..
00001010: 0e 9d
```

Page-2 B-tree header:

| Offset | Bytes | Meaning |
|---|---|---|
| 0x1000 | `0D` | leaf table page |
| 0x1001 | `00 00` | no freeblocks |
| 0x1003 | `00 05` | **5 cells** = 5 rows |
| 0x1005 | `0E 9D` | content area starts at 0x0E9D inside the page (= 0x1E9D in file) |

5 cell pointers follow at **0x1008** (each is a 16-bit offset *within the page*):

| ptr | page-relative | file offset | row |
|---|---|---|---|
| `0F AD` | 0x0FAD | 0x1FAD | **Aman Yadav** (rowid 1) |
| `0F 66` | 0x0F66 | 0x1F66 | **Riya Verma** (rowid 2) |
| `0F 22` | 0x0F22 | 0x1F22 | **Karan Mehta** (rowid 3) |
| `0E DE` | 0x0EDE | 0x1EDE | **Sneha Kapoor** (rowid 4) |
| `0E 9D` | 0x0E9D | 0x1E9D | **Vivaan Sharma** (rowid 5) |

### Aman's row @ 0x1FAD

```
00001fa0: ............................. 45 01 08
00001fb0: 00 15 17 01 2d 2d 33 41 6d 61 6e 59 61 64 61 76  ....--3AmanYadav
00001fc0: 13 61 6d 61 6e 40 65 78 61 6d 70 6c 65 2e 63 6f  .aman@example.co
00001fd0: 6d 43 6f 6d 70 75 74 65 72 20 53 63 69 65 6e 63  mComputer Scienc
```

Cell layout (record-format):

- `45`             → payload size 69
- `01`             → rowid 1
- `08`             → record-header length 8
- header types:
  - `00` NULL (student_id alias of rowid → stored as NULL)
  - `15` text(4)  → "Aman"
  - `17` text(5)  → "Yadav"
  - `01` int1     → age = 19
  - `2D` text(16) → "aman@example.com"
  - `2D` text(16) → "Computer Scienc..." wait, type 0x2D ⇒ length 24… actual layout uses next types
  - `33` text(19) → "2026-05-25 18:51:16" timestamp
- body: `AmanYadavaman@example.comComputer Science2026-05-25 18:51:16`

Reading text-type lengths: SQLite encodes text length as `(N - 13) / 2`.
So `0x15 = 21` → `(21-13)/2 = 4` chars ("Aman"); `0x17 = 23` → 5 chars
("Yadav"); `0x2D = 45` → 16 chars ("aman@example.com"); `0x33 = 51` →
19 chars (the timestamp). The column lengths in the header are what tell
the decoder where each value ends — there is no separator inside the
body.

### Vivaan's row @ 0x1E9D

Same record format, payload starts with `4B 05 08 00 ...` (payload 75, rowid 5).
Body reads `VivaanSharmavivaan@example.comCivil2026-05-25 18:51:16`.

---

## Page 3 — the UNIQUE-email index (0x2000)

```
00002000: 0a 00 00 00 05 0f 88 00 0f e0 0f ca 0f b5 0f 9f  ................
00002010: 0f 88
```

Page-3 B-tree header:

| Offset | Bytes | Meaning |
|---|---|---|
| 0x2000 | `0A` | **leaf index** page (not table) |
| 0x2003 | `00 05` | 5 cells = 5 index entries |
| 0x2005 | `0F 88` | content area starts at 0x0F88 inside the page (= 0x2F88 in file) |

Cell pointers at **0x2008**: 0x0FE0, 0x0FCA, 0x0FB5, 0x0F9F, 0x0F88
(→ file offsets 0x2FE0, 0x2FCA, 0x2FB5, 0x2F9F, 0x2F88).

The index entries at the bottom of page 3:

```
00002f80: ........................ 16 03 31 01 76 69 76 61  ..........1.viva
00002f90: 61 6e 40 65 78 61 6d 70 6c 65 2e 63 6f 6d 05 15  an@example.com..
00002fa0: 03 2f 01 73 6e 65 68 61 40 65 78 61 6d 70 6c 65  ./.sneha@example
00002fb0: 2e 63 6f 6d 04 14 03 2d 01 72 69 79 61 40 65 78  .com...-.riya@ex
00002fc0: 61 6d 70 6c 65 2e 63 6f 6d 02 15 03 2f 01 6b 61  ample.com.../.ka
00002fd0: 72 61 6e 40 65 78 61 6d 70 6c 65 2e 63 6f 6d 03  ran@example.com.
00002fe0: 14 03 2d 01 61 6d 61 6e 40 65 78 61 6d 70 6c 65  ..-.aman@example
00002ff0: 2e 63 6f 6d 01                                   .com.
```

Each index cell is `[payload_size][header_size][type_email][type_rowid][email_bytes][rowid_byte]`.
Reading the entry at 0x2FE0 (alphabetically first, "aman"):

- `14`             → payload size 20
- `03`             → header size 3
- `2D` text(16)    → email column type ("aman@example.com")
- `01` int1        → rowid column type
- body: `"aman@example.com" 01`  → **email "aman@example.com" → rowid 1**

Listing all 5 entries (the index is **sorted by email**):

| index slot | key | rowid |
|---|---|---|
| 0x2FE0 | aman@example.com   | 1 |
| 0x2FCA | karan@example.com  | 3 |
| 0x2FB5 | riya@example.com   | 2 |
| 0x2F9F | sneha@example.com  | 4 |
| 0x2F88 | vivaan@example.com | 5 |

A `SELECT * FROM students WHERE email='riya@example.com'` walks this index
first (one page read → finds rowid 2), then jumps to page 2 and follows the
rowid-table cell pointer for rowid 2 → row at 0x1F66.

---

## B-tree shape

```
Page 1 (sqlite_master, 0x0D leaf table)
   ├── cell[0] → students    rootpage = 2
   └── cell[1] → autoindex_1 rootpage = 3
                      |             |
                      v             v
                Page 2          Page 3
                (0x0D leaf      (0x0A leaf
                 table)          index)
                ─────────       ─────────
                rowid 1 Aman    aman→1
                rowid 2 Riya    karan→3
                rowid 3 Karan   riya→2
                rowid 4 Sneha   sneha→4
                rowid 5 Vivaan  vivaan→5
```

With only 5 rows everything is a single leaf — no interior nodes needed. If
we had thousands of rows SQLite would split into interior pages (`0x05` for
table, `0x02` for index) that hold child page pointers + dividers.

Page-type cheat-sheet:

| byte | page kind |
|---|---|
| `0x0D` | leaf table — has full row data |
| `0x05` | interior table — has child pointers + rowids |
| `0x0A` | leaf index — has key + rowid |
| `0x02` | interior index — has child pointers + key dividers |

---

## Two lookup walkthroughs

**Lookup by rowid: `SELECT * FROM students WHERE student_id = 3`**

1. Open page 1, find `students` in sqlite_master → rootpage = 2.
2. Open page 2 (leaf table). Scan cell-pointer array for the cell whose
   rowid = 3 → cell at 0x1F22.
3. Decode the record → "Karan Mehta, 19, …". **Two page reads.**

**Lookup by indexed column: `SELECT * FROM students WHERE email='riya@example.com'`**

1. Open page 1, find `sqlite_autoindex_students_1` → rootpage = 3.
2. Open page 3 (leaf index). Binary search the sorted email keys → match at
   0x2FB5 with payload rowid = 2.
3. Open page 2 (leaf table). Find the cell with rowid = 2 → 0x1F66.
4. Decode → "Riya Verma, 20, …". **Three page reads** — one extra hop, but
   no full table scan, which is the whole point of the index.

---

## Address summary

| address | what is there |
|---|---|
| 0x0000 | file header (magic, page size, page count …) |
| 0x0064 | page-1 B-tree header |
| 0x006C | page-1 cell pointer array |
| 0x0E8A | students table schema row |
| 0x0FBB | sqlite_autoindex_students_1 schema row |
| 0x1000 | **page 2** — student rows |
| 0x1E9D | Vivaan's row |
| 0x1EDE | Sneha's row |
| 0x1F22 | Karan's row |
| 0x1F66 | Riya's row |
| 0x1FAD | Aman's row |
| 0x2000 | **page 3** — UNIQUE email index |
| 0x2F88 | index entry: vivaan → 5 |
| 0x2FE0 | index entry: aman → 1 |

Full dump: [students.hex](students.hex).
