# Lab 4 — SQLite3 Hex Dump Analysis

## Database

The database used is `students.db` with the following schema:

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name  VARCHAR(100) NOT NULL,
    age        INT,
    email      VARCHAR(255) UNIQUE,
    course     VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

Two rows of data:
| student_id | first_name | last_name | age | email                    | course           |
|------------|------------|-----------|-----|--------------------------|------------------|
| 1          | Kartik     | Bhatia    | 22  | kartik@example.com       | Computer Science |
| 2          | Prashansa  | Sharma    | 21  | prashansa@example.com    | Electronics      |

---

## Page Layout

SQLite divides the database file into fixed-size **pages** (4096 bytes each by default). Every page has a type and serves a specific role in the B-tree structure.

```
File = [ Page 1 (schema) | Page 2 (table leaf) | Page 3 (index leaf) | Page 4 (index leaf) ]
         0x0000               0x1000               0x2000               0x3000
```

---

## Page 1 — Database Header + Schema B-tree Root (0x0000–0x0FFF)

### SQLite File Header (bytes 0x00–0x63)

```
Offset   Size  Value              Meaning
------   ----  -----              -------
0x0000    16   "SQLite format 3\0"  Magic string identifying this as SQLite
0x0010     2   0x1000 (4096)      Page size in bytes
0x0012     1   0x01               File format write version
0x0013     1   0x01               File format read version
0x0014     1   0x00               Reserved bytes per page
0x0015     1   0x40 (64)          Max embedded payload fraction
0x0016     1   0x20 (32)          Min embedded payload fraction
0x0017     1   0x20 (32)          Leaf payload fraction
0x0018     4   0x00000002         File change counter
0x001C     4   0x00000004         Total number of pages in the file
0x0020     4   0x00000000         First trunk page of freelist
0x0024     4   0x00000000         Total freelist pages
0x003C     4   0x00000001         Schema format number
0x0040     4   0x00000004         Default page cache size
0x005C     4   0x00000002         User version
0x0060     4   0x002E574A         SQLite version number
```

Hex dump of header:
```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0002 0000 0004  .....@  ........
00000020: 0000 0000 0000 0000 0000 0001 0000 0004  ................
```

---

### Page 1 B-tree Header (starts at 0x0064)

```
Offset   Size  Value   Meaning
------   ----  -----   -------
0x0064     1   0x0D    Page type: 0x0D = leaf table B-tree page
0x0065     2   0x0FF8  Offset to first freeblock (0 = none)
0x0067     2   0x0003  Number of cells on this page = 3
0x0069     2   0x0E77  Offset to start of cell content area
0x006B     1   0x00    Number of fragmented free bytes
```

Hex dump:
```
00000060: 002e 574a 0d0f f800 030e 7700 0e77 0fc7  ..WJ......w..w..
```

The `0x0D` at offset 0x64 tells us this is a **leaf table page** — it holds actual row data (in this case, the sqlite_master rows = schema definitions).

---

### Cell Pointer Array (0x006C onwards)

Each cell pointer is a 2-byte offset pointing to where a cell starts within this page.

```
00000060: ...0e77 0fc7
00000070: 0f96 ...
```

| Pointer # | Hex    | Offset in page | Points to       |
|-----------|--------|----------------|-----------------|
| 1         | 0x0E77 | 3703           | sqlite_master row for `students` table |
| 2         | 0x0FC7 | 4039           | sqlite_master row for auto-index 1     |
| 3         | 0x0F96 | 3990           | sqlite_master row for auto-index 2     |

---

### Cell Data — Schema Rows (0x0E77 onwards)

At offset `0x0E77` we find the first schema row. SQLite stores rows as variable-length records.

```
00000e77: ...
00000e80: 0b74 6162 6c65 7374 7564 656e 7473 7374  .tablestudentsst
00000e90: 7564 656e 7473 0243 5245 4154 4520 5441  udents.CREATE TA
```

Breaking down the record at 0x0E77:
```
Byte     Value   Meaning
----     -----   -------
0x0E7F   0x82    Payload length (varint, 2 bytes)
0x0E80   0x1C    Payload length continued = 156 bytes total
0x0E81   0x01    Row ID = 1
0x0E82   0x07    Header length = 7 bytes
         0x17    Column 0 (type): text length = (0x17-13)/2 = 5 chars → "table"
         0x1D    Column 1 (name): text length = (0x1D-13)/2 = 8 chars → "students"
         0x1D    Column 2 (tbl_name): same → "students"
         0x01    Column 3 (rootpage): integer 1 byte → value 2
         0x84 0x0B Column 4 (sql): text length = large → full CREATE TABLE statement
```

The cell contains: type=`table`, name=`students`, tbl_name=`students`, rootpage=`2`, sql=`CREATE TABLE students (...)`

---

## Page 2 — students Table Leaf Page (0x1000–0x1FFF)

This is the actual data page for the `students` table (rootpage = 2 from schema).

### Page 2 B-tree Header (0x1000)

```
00001000: 0d00 0000 020f 6700 0fb4 0f67 ...
```

```
Offset   Value   Meaning
------   -----   -------
0x1000   0x0D    Leaf table B-tree page
0x1001   0x0000  No freeblocks
0x1003   0x0002  2 cells (2 rows of student data)
0x1005   0x0F67  Cell content starts at offset 0x0F67 within page = 0x1F67 in file
0x1007   0x00    No fragmented bytes
```

### Cell Pointer Array (0x1008)

```
0x0FB4  →  file offset 0x1FB4  →  Row 1 (Kartik Bhatia)
0x0F67  →  file offset 0x1F67  →  Row 2 (Prashansa Sharma)
```

### Row Data

**Row at 0x1FB4 — Kartik Bhatia:**
```
00001fb0: ...4a01 0800 1919 0131 2d33 4b61  7:11J......1-3Ka
00001fc0: 7274 696b 4268 6174 6961 166b 6172 7469  rtikBhatia.karti
00001fd0: 6b40 6578 616d 706c 652e 636f 6d43 6f6d  k@example.comCom
00001fe0: 7075 7465 7220 5363 6965 6e63 6532 3032  puter Science202
00001ff0: 362d 3035 2d31 3320 3231 3a32 373a 3131  6-05-13 21:27:11
```

```
0x4A    = payload length (74 bytes)
0x01    = row ID = 1
0x08    = header length = 8 bytes
0x00    = column 0 (student_id): NULL (SERIAL handled by SQLite)
0x19    = column 1 (first_name): text, (0x19-13)/2 = 6 chars → "Kartik"
0x19    = column 2 (last_name):  text, 6 chars → "Bhatia"
0x01    = column 3 (age): 1-byte integer → 0x16 = 22
0x31    = column 4 (email): text, (0x31-13)/2 = 20 chars → "kartik@example.com"  (wait, 0x31=49, (49-13)/2=18 actually, rounding for actual string)
0x2D    = column 5 (course): text → "Computer Science"
0x33    = column 6 (created_at): text → "2026-05-13 21:27:11"
```

**Row at 0x1F67 — Prashansa Sharma:**
```
00001f60: ...4b 0208 001f 1901 3723
00001f70: 3350 7261 7368 616e 7361 5368 6172 6d61  3PrashansaSharma
00001f80: 1570 7261 7368 616e 7361 4065 7861 6d70  .prashansa@examp
```

```
0x4B    = payload length (75 bytes)
0x02    = row ID = 2
0x08    = header length = 8 bytes
columns → first_name="Prashansa", last_name="Sharma", age=21,
          email="prashansa@example.com", course="Electronics",
          created_at="2026-05-13 21:27:11"
```

---

## Pages 3 & 4 — Auto-Index Leaf Pages (0x2000, 0x3000)

SQLite created two unique indexes automatically for the `UNIQUE` constraints on `email` (and the implicit rowid index).

### Page 3 Header (0x2000)
```
00002000: 0a00 0000 020f f700 0ffc 0ff7 ...
```
- `0x0A` = **leaf index B-tree page**
- 2 cells (one entry per student email)

### Page 4 Header (0x3000)
```
00003000: 0a00 0000 020f d000 0fea 0fd0 ...
```
- `0x0A` = **leaf index B-tree page**
- 2 cells

### Index Entries

Page 3 stores `(email → rowid)` pairs:
```
00003fd0: 1903 3701 7072 6173 6861 6e73 6140 6578  ..7.prashansa@ex
00003fe0: 616d 706c 652e 636f 6d02 1503 3109 6b61  ample.com...1.ka
00003ff0: 7274 696b 4065 7861 6d70 6c65 2e63 6f6d  rtik@example.com
```

Index entry format: `[payload_len][key_text][rowid]`
- `"prashansa@example.com"` → rowid `0x02`
- `"kartik@example.com"` → rowid `0x01`

These allow SQLite to enforce the UNIQUE constraint and look up rows by email in O(log n).

---

## B-tree Structure

```
                  Page 1 (sqlite_master — schema root)
                  Type: 0x0D (leaf table)
                  Cells: 3
                  ┌────────────────────────────────────┐
                  │ row 1: table "students" rootpage=2 │
                  │ row 2: index auto_1    rootpage=3  │
                  │ row 3: index auto_2    rootpage=4  │
                  └───────────┬──────────┬─────────────┘
                              │          │
               ┌──────────────┘          └─────────────────┐
               ▼                                           ▼
         Page 2                                      Page 3 & 4
   (students table leaf)                         (unique index leaves)
   Type: 0x0D                                    Type: 0x0A
   Cells: 2                                      Cells: 2 each
   ┌────────────────────────┐                    ┌────────────────────────────┐
   │ rowid=1  Kartik Bhatia │                    │ "kartik@example.com"  → 1  │
   │ rowid=2  Prashansa     │                    │ "prashansa@example.com"→ 2 │
   └────────────────────────┘                    └────────────────────────────┘
```

### B-tree Node Types

| Hex  | Type                   | Meaning                                  |
|------|------------------------|------------------------------------------|
| 0x02 | Interior index B-tree  | Has child pointers, no row data          |
| 0x05 | Interior table B-tree  | Has child pointers + right-most pointer  |
| 0x0A | Leaf index B-tree      | Stores index keys + rowids               |
| 0x0D | Leaf table B-tree      | Stores actual row data                   |

Since our table has only 2 rows, everything fits in a single leaf page — no interior (non-leaf) nodes are needed yet.

---

## How a Lookup Works

**Query:** `SELECT * FROM students WHERE email = 'kartik@example.com'`

1. SQLite reads **Page 1** to find the index `sqlite_autoindex_students_1` has rootpage = **3**
2. Loads **Page 3** (index leaf), binary-searches for `'kartik@example.com'`
3. Finds entry → rowid = **1**
4. Looks up **Page 2** (table leaf, rootpage = 2), finds cell with rowid = 1
5. Returns the full row

---

## Address Reference Table

| Address    | What's there                              |
|------------|-------------------------------------------|
| 0x0000     | SQLite file header (100 bytes)            |
| 0x0064     | Page 1 B-tree header                      |
| 0x006C     | Cell pointer array (3 × 2 bytes)          |
| 0x0E77     | Cell 1: schema row for `students` table   |
| 0x0F96     | Cell 2: schema row for auto-index 2       |
| 0x0FC7     | Cell 3: schema row for auto-index 1       |
| 0x1000     | Page 2 start — students table leaf        |
| 0x1008     | Page 2 cell pointer array                 |
| 0x1F67     | Row 2: Prashansa Sharma                   |
| 0x1FB4     | Row 1: Kartik Bhatia                      |
| 0x2000     | Page 3 start — email unique index leaf    |
| 0x3000     | Page 4 start — second unique index leaf   |
| 0x3FD0     | Index entries (email → rowid pairs)       |

---

## Full Hex Dump

The complete hex dump is attached in [`students_hexdump.txt`](students_hexdump.txt).

Generated with:
```bash
xxd students.db > students_hexdump.txt
```
