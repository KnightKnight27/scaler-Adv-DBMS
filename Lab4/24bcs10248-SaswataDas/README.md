# Lab 4 — SQLite On-Disk Format Walkthrough

**Name.** Saswata Das
**Roll No.** 24BCS10248

A byte-level walkthrough of how SQLite stores a small `students` table on
disk. The DB has 5 rows and one auto-created UNIQUE index on `email`. After
`VACUUM` the file is exactly **3 pages x 4096 B = 12 288 B**.

## Files

| File | Purpose |
|---|---|
| `students.sql` | DDL + 5 inserts + `VACUUM` |
| `students.db`  | 12 288 B SQLite database |
| `students.hex` | full `xxd -g 1 -c 16` dump |
| `README.md`    | this walkthrough |

## Reproduce

```bash
cd Lab4/24bcs10248-SaswataDas
rm -f students.db
sqlite3 students.db < students.sql
xxd -g 1 -c 16 students.db > students.hex
```

## How SQLite lays out the file

SQLite splits the file into fixed-size pages (4096 B here). Every page is one
of: leaf table (`0x0D`), interior table (`0x05`), leaf index (`0x0A`), or
interior index (`0x02`). Our DB has 3 pages:

```
0x0000  page 1  -> sqlite_master (schema catalog)             leaf table (0x0D)
0x1000  page 2  -> students rows                              leaf table (0x0D)
0x2000  page 3  -> sqlite_autoindex_students_1 (UNIQUE email) leaf index (0x0A)
```

---

## Page 1 — the 100-byte file header (0x0000)

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 0c 40 20 20 00 00 00 03 00 00 00 03  .....@  ........
```

Key header fields:

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
0x006C: 0E 8A   -> students table schema row    @ file offset 0x0E8A
0x006E: 0F BB   -> sqlite_autoindex_1 row       @ file offset 0x0FBB
```

### students table schema cell @ 0x0E8A

```
00000e8a: 82 2e 01 07 17 1d 1d 01 84 2f 74 61 62 6c 65 73  ........./tables
00000e9a: 74 75 64 65 6e 74 73 73 74 75 64 65 6e 74 73 02  tudentsstudents.
```

You can literally read `tablestudentsstudents` followed by the full
`CREATE TABLE students (...)` SQL. The record layout is:

- `82 2e` -> payload size (varint, decodes to 302 bytes)
- `01`    -> rowid 1
- `07`    -> header length 7
- `17 1d 1d 01 84 2f` -> serial-type codes for the 5 columns
  - `0x17` text(5)  -> "table"
  - `0x1D` text(8)  -> "students"
  - `0x1D` text(8)  -> "students" (tbl_name)
  - `0x01` int1     -> rootpage = 2  <-- **the students rows live on page 2**
  - `0x84 2F` text  -> the CREATE TABLE source

### autoindex schema cell @ 0x0FBB

```
00000fba: 29 2f 02 06 17 43 1d 01 00 69 6e 64 65 78 73 71  )/...C...indexsq
00000fca: 6c 69 74 65 5f 61 75 74 6f 69 6e 64 65 78 5f 73  lite_autoindex_s
00000fda: 74 75 64 65 6e 74 73 5f 31 73 74 75 64 65 6e 74  tudents_1student
00000fea: 73 03                                             s.
```

- `2F` -> payload size 47
- `02` -> rowid 2
- `06 17 43 1d 01 00` -> header: text(5)="index", text(27)="sqlite_autoindex_students_1", text(8)="students", int1=3 (rootpage), NULL (no SQL)
- `03` -> **rootpage 3** <-- the index lives on page 3

---

## Page 2 — the actual students rows (0x1000)

```
00001000: 0d 00 00 00 05 0e 96 00 0f a3 0f 59 0f 15 0e d4  ...........Y....
00001010: 0e 96
```

Page-2 B-tree header:

| Offset | Bytes | Meaning |
|---|---|---|
| 0x1000 | `0D` | leaf table page |
| 0x1001 | `00 00` | no freeblocks |
| 0x1003 | `00 05` | **5 cells** = 5 rows |
| 0x1005 | `0E 9C` | content area starts at 0x0E9C inside the page (= 0x1E9C in file) |

5 cell pointers follow at **0x1008** (each is a 16-bit offset *within the page*):

| ptr | page-relative | file offset | row |
|---|---|---|---|
| `0F A9` | 0x0FA9 | 0x1FA9 | **Saswata Das** (rowid 1) |
| `0F 5F` | 0x0F5F | 0x1F5F | **Priya Sharma** (rowid 2) |
| `0F 1B` | 0x0F1B | 0x1F1B | **Arjun Patel** (rowid 3) |
| `0E DA` | 0x0EDA | 0x1EDA | **Neha Gupta** (rowid 4) |
| `0E 9C` | 0x0E9C | 0x1E9C | **Rohan Singh** (rowid 5) |

### Saswata's row @ 0x1FA9

```
00001fa0: 20 31 37 3a 30 31 3a 33 31 49 01 08 00 1b 13 01   17:01:31I......
00001fb0: 33 2d 33 53 61 73 77 61 74 61 44 61 73 14 73 61  3-3SaswataDas.sa
00001fc0: 73 77 61 74 61 40 65 78 61 6d 70 6c 65 2e 63 6f  swata@example.co
00001fd0: 6d 43 6f 6d 70 75 74 65 72 20 53 63 69 65 6e 63  mComputer Scienc
00001fe0: 65 32 30 32 36 2d 30 35 2d 32 36 20 31 37 3a 30  e2026-05-26 17:0
00001ff0: 31 3a 33 31                                      1:31
```

Cell layout (record-format):

- `49`             -> payload size 73
- `01`             -> rowid 1
- `08`             -> record-header length 8
- header types:
  - `00` NULL (student_id alias of rowid -> stored as NULL)
  - `1B` text(7)  -> "Saswata"
  - `13` text(3)  -> "Das"
  - `01` int1     -> age = 20
  - `33` text(19) -> "saswata@example.com"
  - `2D` text(16) -> "Computer Science"
  - `33` text(19) -> "2026-05-26 17:01:31" timestamp
- body: `SaswataDassaswata@example.comComputer Science2026-05-26 17:01:31`

Reading text-type lengths: SQLite encodes text length as `(N - 13) / 2`.
So `0x1B = 27` -> `(27-13)/2 = 7` chars ("Saswata"); `0x13 = 19` -> 3 chars
("Das"); `0x33 = 51` -> 19 chars ("saswata@example.com"). The column
lengths in the header tell the decoder where each value ends — there is no
separator inside the body.

### Rohan's row @ 0x1E9C

Same record format, payload starts with `3C 05 08 00 ...` (payload 60, rowid 5).
Body reads `RohanSinghrohan@example.comCivil2026-05-26 17:01:31`.

---

## Page 3 — the UNIQUE-email index (0x2000)

```
00002000: 0a 00 00 00 05 0f 86 00 0f de 0f c9 0f b3 0f 9d  ................
00002010: 0f 86
```

Page-3 B-tree header:

| Offset | Bytes | Meaning |
|---|---|---|
| 0x2000 | `0A` | **leaf index** page (not table) |
| 0x2003 | `00 05` | 5 cells = 5 index entries |
| 0x2005 | `0F 86` | content area starts at 0x0F86 inside the page (= 0x2F86 in file) |

Cell pointers at **0x2008**: 0x0FDE, 0x0FC9, 0x0FB3, 0x0F9D, 0x0F86
(-> file offsets 0x2FDE, 0x2FC9, 0x2FB3, 0x2F9D, 0x2F86).

The index entries at the bottom of page 3:

```
00002f86: 16 03 33 09 73 61 73 77 61 74 61 40 65 78 61 6d  ..3.saswata@exam
00002f96: 70 6c 65 2e 63 6f 6d 15 03 2f 01 72 6f 68 61 6e  ple.com../.rohan
00002fa6: 40 65 78 61 6d 70 6c 65 2e 63 6f 6d 05 15 03 2f  @example.com.../
00002fb6: 01 70 72 69 79 61 40 65 78 61 6d 70 6c 65 2e 63  .priya@example.c
00002fc6: 6f 6d 02 14 03 2d 01 6e 65 68 61 40 65 78 61 6d  om...-.neha@exam
00002fd6: 70 6c 65 2e 63 6f 6d 04 15 03 2f 01 61 72 6a 75  ple.com.../.arju
00002fe6: 6e 40 65 78 61 6d 70 6c 65 2e 63 6f 6d 03        n@example.com.
```

Reading the entry at 0x2FDE (alphabetically first, "arjun"):

- `15`             -> payload size 21
- `03`             -> header size 3
- `2F` text(17)    -> email column type ("arjun@example.com")
- `01` int1        -> rowid column type
- body: `"arjun@example.com" 03`  -> **email "arjun@example.com" -> rowid 3**

Listing all 5 entries (the index is **sorted by email**):

| index slot | key | rowid |
|---|---|---|
| 0x2FDE | arjun@example.com   | 3 |
| 0x2FC9 | neha@example.com    | 4 |
| 0x2FB3 | priya@example.com   | 2 |
| 0x2F9D | rohan@example.com   | 5 |
| 0x2F86 | saswata@example.com | 1 |

A `SELECT * FROM students WHERE email='priya@example.com'` walks this index
first (one page read -> finds rowid 2), then jumps to page 2 and follows the
rowid-table cell pointer for rowid 2 -> row at 0x1F5F.

---

## B-tree shape

```
Page 1 (sqlite_master, 0x0D leaf table)
   |-- cell[0] -> students    rootpage = 2
   +-- cell[1] -> autoindex_1 rootpage = 3
                      |             |
                      v             v
                Page 2          Page 3
                (0x0D leaf      (0x0A leaf
                 table)          index)
                ---------       ---------
                rowid 1 Saswata arjun->3
                rowid 2 Priya   neha->4
                rowid 3 Arjun   priya->2
                rowid 4 Neha    rohan->5
                rowid 5 Rohan   saswata->1
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

1. Open page 1, find `students` in sqlite_master -> rootpage = 2.
2. Open page 2 (leaf table). Scan cell-pointer array for the cell whose
   rowid = 3 -> cell at 0x1F1B.
3. Decode the record -> "Arjun Patel, 19, ...". **Two page reads.**

**Lookup by indexed column: `SELECT * FROM students WHERE email='priya@example.com'`**

1. Open page 1, find `sqlite_autoindex_students_1` -> rootpage = 3.
2. Open page 3 (leaf index). Binary search the sorted email keys -> match at
   0x2FB3 with payload rowid = 2.
3. Open page 2 (leaf table). Find the cell with rowid = 2 -> 0x1F5F.
4. Decode -> "Priya Sharma, 21, ...". **Three page reads** — one extra hop, but
   no full table scan, which is the whole point of the index.

---

## Address summary

| address | what is there |
|---|---|
| 0x0000 | file header (magic, page size, page count ...) |
| 0x0064 | page-1 B-tree header |
| 0x006C | page-1 cell pointer array |
| 0x0E8A | students table schema row |
| 0x0FBB | sqlite_autoindex_students_1 schema row |
| 0x1000 | **page 2** — student rows |
| 0x1E9C | Rohan's row |
| 0x1EDA | Neha's row |
| 0x1F1B | Arjun's row |
| 0x1F5F | Priya's row |
| 0x1FA9 | Saswata's row |
| 0x2000 | **page 3** — UNIQUE email index |
| 0x2F86 | index entry: saswata -> 1 |
| 0x2FDE | index entry: arjun -> 3 |

Full dump: [students.hex](students.hex).
