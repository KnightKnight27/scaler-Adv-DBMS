# Lab 4 - SQLite Hex & B-Tree Internal Structure Analysis

**Tanishq | 24BCS10303**

---

## What this is

Used `xxd` to dump the binary content of a SQLite database and traced the actual on-disk layout of the B-Tree structures — page headers, cell pointer arrays, record payloads. The goal was to see exactly how SQLite physically organizes table data and index entries, not just understand it abstractly.

---

## Setup

Created the database from `create_campus.sql`:

```bash
sqlite3 campus.db < create_campus.sql
xxd campus.db > campus.hex
```

Schema:
- Table: `students` (id INTEGER PK, name TEXT, grade INTEGER, department TEXT)
- Index: `idx_students_grade` on `students(grade)`
- 15 rows inserted, `VACUUM` run to compact pages

---

## File overview

```
Assignment-4/
├── campus.db          <- the actual SQLite database (binary)
├── campus.hex         <- full xxd dump of campus.db
├── create_campus.sql  <- SQL used to build the DB
└── README.md          <- this file
```

---

## Page layout

SQLite stores everything in fixed-size pages. Here the page size is **4096 bytes** (confirmed via `PRAGMA page_size`). The file has **3 pages total** (`PRAGMA page_count = 3`).

```
Byte offset     Page    Contents
-----------     ----    --------
0               1       DB file header (100 bytes) + sqlite_master leaf
4096            2       students table — B-Tree leaf page
8192            3       idx_students_grade — B-Tree leaf page
```

---

## Page 1 — DB file header + sqlite_master

### File header (bytes 0–99)

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
```

The first 16 bytes are literally the ASCII string `"SQLite format 3\0"`. That's the magic number SQLite uses to identify its own files. If this is missing or corrupted, SQLite refuses to open the file.

Key header fields decoded:

| Offset | Bytes | Value | Meaning |
|--------|-------|-------|---------|
| 16–17  | `10 00` | 4096 | Page size (16-bit big-endian, `0x1000 = 4096`) |
| 18     | `01`   | 1    | File format write version |
| 19     | `01`   | 1    | File format read version |
| 20     | `00`   | 0    | Reserved bytes per page |
| 24–27  | `00000004` | 4 | File change counter |
| 28–31  | `00000003` | 3 | Page count in file |
| 56–59  | `00000004` | 4 | Application ID / schema cookie |
| 60–63  | `002e8df8` | — | SQLite version number (3.51.0) |

### B-Tree page header for page 1 (starts at byte 100)

```
00000060: 002e 8df8 0d00 0000 020f 0600 0f60 0f06
```

The line above starts at file offset `0x60 = 96`. The first 4 bytes (`002e8df8`) are the tail end of the 100-byte file header (SQLite version). The B-Tree page header kicks in at byte 100 (`0x64` within this line, which is `0x60 + 4`):

| File offset | Value | Meaning |
|-------------|-------|---------|
| 100 (`0x64`) | `0d`  | Page type = **0x0D = leaf table B-Tree page** |
| 101–102      | `0000`| First freeblock offset = 0 (no free blocks) |
| 103–104      | `0002`| Number of cells = 2 (sqlite_master has 2 entries) |
| 105–106      | `0f06`| Cell content area starts at offset `0x0F06 = 3846` |
| 107          | `00`  | Fragmented free bytes = 0 |

The cell pointer array follows immediately. Two 2-byte big-endian offsets:
- `0f60` → cell at offset `0x0F60 = 3936` (the `students` table entry)
- `0f06` → cell at offset `0x0F06 = 3846` (the `idx_students_grade` index entry)

These point to the record payloads stored near the end of the page (SQLite fills pages from the bottom up, cell pointers grow down from offset 108).

---

## Page 2 — students table (B-Tree leaf)

Page starts at file offset `4096 = 0x1000`.

```
00001000: 0d00 0000 0f0f 1100 0fe4 0fd6 0fc6 0fb7  ................
00001010: 0fa9 0f99 0f89 0f79 0f6b 0f5d 0f4d 0f3e
00001020: 0f30 0f21 0f11 ...
```

### Page header

| Offset | Value | Meaning |
|--------|-------|---------|
| 0      | `0d`  | Page type = leaf table B-Tree |
| 1–2    | `0000`| No free blocks |
| 3–4    | `000f`| **15 cells** (15 student rows) |
| 5–6    | `0f11`| Content area starts at `0x0F11 = 3857` |
| 7      | `00`  | Fragmented bytes = 0 |

### Cell pointer array (bytes 8–37)

15 two-byte pointers, each pointing to one row record:

```
0fe4 0fd6 0fc6 0fb7 0fa9 0f99 0f89 0f79
0f6b 0f5d 0f4d 0f3e 0f30 0f21 0f11
```

Converting to decimal offsets within the page:
- Row 1 → offset 4068
- Row 2 → offset 4054
- Row 3 → offset 4038
- ...continuing downward
- Row 15 → offset 3857

Each cell is a varint-encoded length + rowid + record payload. The records pack the columns (name TEXT, grade INTEGER, department TEXT) using SQLite's type affinity encoding. Integer values up to 127 fit in 1 byte (varint type code `01`), strings use type code `2*len + 13`.

### B-Tree structure for students table

Since there are only 15 rows and SQLite's default fill factor keeps a leaf page well under its max capacity, the entire table fits in **one leaf page**. No interior nodes needed at this data volume. The B-Tree for `students` is just a single leaf.

---

## Page 3 — idx_students_grade (B-Tree index leaf)

Page starts at file offset `8192 = 0x2000`.

```
00002000: 0a00 0000 0f0f 9b00 0fee 0fe8 0fe2 0fdc
00002010: 0fd6 0fd0 0fca 0fc4 0fbe 0fb8 0fb2 0fad
00002020: 0fa7 0fa1 0f9b ...
```

### Page header

| Offset | Value | Meaning |
|--------|-------|---------|
| 0      | `0a`  | Page type = **0x0A = leaf index B-Tree page** |
| 1–2    | `0000`| No free blocks |
| 3–4    | `000f`| 15 cells (one per row in students) |
| 5–6    | `0f9b`| Content area starts at `0x0F9B = 3995` |
| 7      | `00`  | Fragmented bytes = 0 |

The `0x0a` page type is the index variant of `0x0d`. Key difference: index leaf cells store `(indexed_value, rowid)` pairs instead of full row records. So each entry here has the `grade` integer and the corresponding `rowid` — that's what lets SQLite do index range scans without touching the main table.

The 15 index cells are compactly packed starting near `0x0F9B`. The grades range from 76 to 95 and they're stored in sorted order (B-Tree property), so `SELECT * FROM students WHERE grade BETWEEN 80 AND 90` can use this index to binary-search the right range rather than scanning all 15 rows.

---

## Schema entries decoded

```sql
sqlite3 campus.db "SELECT type, name, tbl_name, rootpage FROM sqlite_master;"
```

```
table | students          | students | 2
index | idx_students_grade| students | 3
```

- `rootpage = 2` for `students` → root of that B-Tree is page 2
- `rootpage = 3` for the index → root is page 3

Both are leaf pages (no interior nodes) because 15 rows don't need a split. At ~100+ rows SQLite would create interior (interior B-Tree) pages and the tree would grow.

---

## Key observations

**1. Pages grow bottom-up.** Cell content is written starting from the end of the page toward the beginning. The cell pointer array grows forward from offset 8 (or 108 on page 1 due to the file header). They meet in the middle when the page is full.

**2. Table vs index page types differ.** `0x0D` is table leaf, `0x0A` is index leaf. Table leaves store full records. Index leaves store only `(key, rowid)` — smaller, so indexes pack more entries per page.

**3. VACUUM compacted the file.** Before `VACUUM`, SQLite might have used temporary pages for intermediate operations. After, the file is 3 clean pages — exactly what the data requires. The freelist count is 0.

**4. The B-Tree header lives inside the page.** Page 1 is special because the 100-byte file header takes the first slot, so the B-Tree header for page 1 starts at byte 100 (`0x64`), not byte 0 like pages 2 and 3.

**5. Integer encoding is tight.** SQLite doesn't waste space on integers that fit in fewer bytes. A grade like `76` fits in 1 byte and the type code in the record header reflects that. Longer strings like department names use 2*len+13 as their type code.

---

## How the hex connects to the B-Tree

The actual B-Tree in a SQLite file looks like this at 15 rows:

```
Page 1 (sqlite_master leaf)
  ├── cell: students table definition  (rootpage=2)
  └── cell: idx_students_grade entry  (rootpage=3)

Page 2 (students table leaf)
  └── 15 row records, sorted by rowid (1..15)

Page 3 (index leaf)
  └── 15 (grade, rowid) pairs, sorted by grade (76..95)
```

A query like `SELECT name FROM students WHERE grade = 89` would:
1. Start at rootpage=3 (the index)
2. Binary search the index leaf for grade=89
3. Find the rowid (7 in this case, for Grace)
4. Go to rootpage=2 (the table)
5. Binary search the table leaf for rowid=7
6. Return the `name` field

Two B-Tree lookups, no full table scan. That's why indexes matter.

---

## Commands used

```bash
# create the database
sqlite3 campus.db < create_campus.sql

# full hex dump
xxd campus.db > campus.hex

# inspect just the file header
xxd -l 100 campus.db

# inspect page 2 (table, starts at offset 4096)
xxd -s 4096 -l 200 campus.db

# inspect page 3 (index, starts at offset 8192)
xxd -s 8192 -l 200 campus.db

# check page size, count, freelist
sqlite3 campus.db "PRAGMA page_size;"
sqlite3 campus.db "PRAGMA page_count;"
sqlite3 campus.db "PRAGMA freelist_count;"

# check rootpage assignments
sqlite3 campus.db "SELECT type, name, tbl_name, rootpage FROM sqlite_master;"
```
