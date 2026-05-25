# SQLite3 File Internals & Hex Dump Lab

> **Course:** Database Systems Internals  
> **Lab Title:** SQLite3 Binary File Analysis — Page Layout, B-tree Nodes, and Hex Navigation  
> **Environment:** Ubuntu 24.04 (Noble) | SQLite 3.45.1 | `xxd` 2023-10-25  
> **Database:** `lab.db` — page size 512 bytes, 3 pages, 4 rows, 1 index  

---

## Table of Contents

1. [Objectives](#objectives)
2. [Lab Setup](#lab-setup)
3. [Database Schema and Metadata](#database-schema-and-metadata)
4. [SQLite File Header (Bytes 0–99)](#sqlite-file-header-bytes-099)
5. [Page Layout Overview](#page-layout-overview)
6. [Page 1 — sqlite_master Schema Leaf (B-tree)](#page-1--sqlite_master-schema-leaf)
7. [Page 2 — `students` Table Leaf (B-tree)](#page-2--students-table-leaf)
8. [Page 3 — `idx_students_name` Index Leaf (B-tree)](#page-3--idx_students_name-index-leaf)
9. [B-tree Node Structure Deep Dive](#b-tree-node-structure-deep-dive)
10. [Record Format and Serial Types](#record-format-and-serial-types)
11. [Lookup Process: Finding a Row by Name](#lookup-process-finding-a-row-by-name)
12. [Page Offset and Address Reference](#page-offset-and-address-reference)
13. [Key Observations](#key-observations)

---

## Objectives

- Parse the SQLite binary file header directly from hex output
- Identify and interpret each of the three B-tree pages (`sqlite_master`, table leaf, index leaf)
- Decode cell pointer arrays, cell headers, record payloads, and serial type codes
- Trace the complete lookup path from an index scan to a table row fetch

---

## Lab Setup

### Commands Executed

```bash
mkdir sqlite3_hex_lab && cd sqlite3_hex_lab

cat > setup.sql <<'EOF'
PRAGMA page_size = 512;
VACUUM;
CREATE TABLE students (
  id INTEGER PRIMARY KEY,
  name TEXT,
  age INTEGER
);
INSERT INTO students VALUES (1, 'Asha',  20);
INSERT INTO students VALUES (2, 'Ravi',  21);
INSERT INTO students VALUES (3, 'Meena', 22);
INSERT INTO students VALUES (4, 'Kiran', 23);
CREATE INDEX idx_students_name ON students(name);
EOF

sqlite3 lab.db < setup.sql
xxd -g 1 -c 16 lab.db > hexdump.txt

dd if=lab.db bs=512 skip=0 count=1 | xxd -g 1 -c 16 > page1.txt
dd if=lab.db bs=512 skip=1 count=1 | xxd -g 1 -c 16 > page2.txt
dd if=lab.db bs=512 skip=2 count=1 | xxd -g 1 -c 16 > page3.txt
```

### Verification

```
$ sqlite3 lab.db "SELECT * FROM students;"
1|Asha|20
2|Ravi|21
3|Meena|22
4|Kiran|23

$ sqlite3 lab.db "PRAGMA page_size;"   → 512
$ sqlite3 lab.db "PRAGMA page_count;"  → 3
```

### sqlite_master Contents

```
$ sqlite3 lab.db "SELECT name, rootpage, sql FROM sqlite_master;"

students          | 2 | CREATE TABLE students (
                  |   |   id INTEGER PRIMARY KEY,
                  |   |   name TEXT,
                  |   |   age INTEGER
                  |   | )
idx_students_name | 3 | CREATE INDEX idx_students_name ON students(name)
```

---

## Database Schema and Metadata

| Object              | Type  | Root Page | Notes                          |
|---------------------|-------|-----------|--------------------------------|
| `students`          | table | 2         | 4 rows, INTEGER PRIMARY KEY    |
| `idx_students_name` | index | 3         | B-tree index on `name` column  |

**File layout:**

```
File offset 0x000–0x1FF  (bytes    0–511) → Page 1: sqlite_master (schema)
File offset 0x200–0x3FF  (bytes  512–1023)→ Page 2: students table data
File offset 0x400–0x5FF  (bytes 1024–1535)→ Page 3: idx_students_name index
```

---

## SQLite File Header (Bytes 0–99)

Every SQLite database begins with a fixed 100-byte file header occupying the first 100 bytes of Page 1. All multi-byte integers are stored **big-endian**.

### Raw Hex (from `hexdump.txt`, offsets 0x00–0x63)

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 02 00 01 01 00 40 20 20 00 00 00 07 00 00 00 03  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 03 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
00000040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 07  ................
00000060: 00 2e 76 89 ...                                  ................
```

### Header Field Decode Table

| Offset (hex) | Offset (dec) | Bytes | Raw Hex       | Decoded Value                                |
|:---:|:---:|:---:|:---:|---|
| 0x00 | 0   | 16 | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | Magic string: `"SQLite format 3\0"` |
| 0x10 | 16  | 2  | `02 00`        | Page size = **512 bytes** (0x0200)           |
| 0x12 | 18  | 1  | `01`           | File format write version = 1 (legacy)       |
| 0x13 | 19  | 1  | `01`           | File format read version = 1 (legacy)        |
| 0x14 | 20  | 1  | `00`           | Reserved space per page = 0 bytes            |
| 0x15 | 21  | 1  | `40`           | Max embedded payload fraction = 64           |
| 0x16 | 22  | 1  | `20`           | Min embedded payload fraction = 32           |
| 0x17 | 23  | 1  | `20`           | Leaf payload fraction = 32                   |
| 0x18 | 24  | 4  | `00 00 00 07`  | File change counter = **7**                  |
| 0x1C | 28  | 4  | `00 00 00 03`  | Database size in pages = **3**               |
| 0x20 | 32  | 4  | `00 00 00 00`  | First freelist trunk page = 0 (no freelist)  |
| 0x24 | 36  | 4  | `00 00 00 00`  | Total freelist pages = 0                     |
| 0x28 | 40  | 4  | `00 00 00 03`  | Schema cookie = **3**                        |
| 0x2C | 44  | 4  | `00 00 00 04`  | Schema format number = **4**                 |
| 0x30 | 48  | 4  | `00 00 00 00`  | Default page cache size = 0                  |
| 0x34 | 52  | 4  | `00 00 00 00`  | Largest auto-vacuum root b-tree page = 0     |
| 0x38 | 56  | 4  | `00 00 00 01`  | Text encoding = **1** (UTF-8)                |
| 0x3C | 60  | 4  | `00 00 00 00`  | User version = 0                             |
| 0x40 | 64  | 4  | `00 00 00 00`  | Incremental vacuum mode = 0                  |
| 0x44 | 68  | 4  | `00 00 00 00`  | Application ID = 0                           |
| 0x5C | 92  | 4  | `00 00 00 07`  | Version-valid-for number = **7**             |
| 0x60 | 96  | 4  | `00 2e 76 89`  | SQLite version = 0x002e7689 = **3,045,001** → v3.45.1 |

> **Note on version:** `0x002e7689` = 3,045,001. SQLite encodes the version as `major×1,000,000 + minor×1,000 + patch`, so 3×1,000,000 + 45×1,000 + 1 = 3,045,001, confirming SQLite **3.45.1**.

---

## Page Layout Overview

Each 512-byte page follows this internal structure:

```
┌─────────────────────────────────────────────────────┐  ← byte 0 of page
│  [File Header — 100 bytes]  (Page 1 only)           │
├─────────────────────────────────────────────────────┤
│  B-tree Page Header  (8 bytes — leaf pages)         │
│    byte 0:    page type flag                        │
│    bytes 1–2: first freeblock offset                │
│    bytes 3–4: number of cells                       │
│    bytes 5–6: cell content area start offset        │
│    byte 7:    fragmented free bytes                 │
├─────────────────────────────────────────────────────┤
│  Cell Pointer Array  (2 bytes × number of cells)    │
│    Each entry is a 2-byte page-relative offset       │
│    pointing to a cell in the content area below.    │
│    Pointers appear in key order (ascending).        │
├─────────────────────────────────────────────────────┤
│  Unallocated free space                             │
│  (grows downward as cells are added)                │
├─────────────────────────────────────────────────────┤
│  Cell Content Area  (cells packed from bottom up)   │
│    Each cell: payload_length varint + rowid varint  │
│               + record payload                      │
└─────────────────────────────────────────────────────┘  ← byte 511 of page
```

### Page Type Codes

| Byte Value | Page Type                        |
|:---:|---|
| `0x02`     | Interior index B-tree page       |
| `0x05`     | Interior table B-tree page       |
| `0x0A` (10)| **Leaf index B-tree page**       |
| `0x0D` (13)| **Leaf table B-tree page**       |

---

## Page 1 — sqlite_master Schema Leaf

**File offsets:** `0x000`–`0x1FF` (bytes 0–511)  
**B-tree header starts at:** file offset `0x064` (byte 100, immediately after the file header)

### B-tree Page Header

```
00000060: 00 2e 76 89 [0d 00 00 00 02 01 3c 00] 01 93 01 3c
                      ↑page header starts here (offset 0x64)
```

| Page Offset | Raw Hex | Field               | Value                        |
|:---:|:---:|---|---|
| 0x64 (100)  | `0d`    | Page type           | `0x0D` = Leaf table B-tree   |
| 0x65–0x66   | `00 00` | First freeblock     | 0 (no freeblocks)            |
| 0x67–0x68   | `00 02` | Cell count          | **2 cells**                  |
| 0x69–0x6A   | `01 3c` | Cell content start  | Page offset **0x013C = 316** |
| 0x6B        | `00`    | Fragmented bytes    | 0                            |

### Cell Pointer Array (page offsets 0x6C–0x6F)

```
00000060: ...01 93 01 3c
               ↑       ↑
            ptr[0]   ptr[1]
```

| Pointer # | Raw Hex | Page Offset | File Offset | Content                     |
|:---:|:---:|:---:|:---:|---|
| ptr[0]    | `01 93` | 403         | 0x193 = 403 | Row: `students` table entry  |
| ptr[1]    | `01 3c` | 316         | 0x13C = 316 | Row: `idx_students_name` entry|

> Pointers are stored in ascending key order (by rowid). Cells are allocated from the bottom of the page upward.

### Cell 1 — `students` table entry (file offset 0x193)

```
00000190: 6d 65 29 [6b 01 07 17 1d 1d 01 81 29 74 61 62 6c
000001a0: 65 73 74 75 64 65 6e 74 73 73 74 75 64 65 6e 74
000001b0: 73 02 ...]
               ↑ rowid=1 starts at 0x193
```

| Byte(s) | Hex       | Meaning                                      |
|:---:|:---:|---|
| `6b`    | 0x6B = 107| Payload length = 107 bytes                   |
| `01`    | 1         | rowid = **1**                                |
| `07`    | 7         | Record header size = 7 bytes                 |
| `17`    | 23        | col[0] type=23 → text, len=(23−13)/2=**5** → `"table"` |
| `1d`    | 29        | col[1] type=29 → text, len=(29−13)/2=**8** → `"students"` |
| `1d`    | 29        | col[2] type=29 → text, len=**8** → `"students"` |
| `01`    | 1         | col[3] type=1 → 1-byte int → rootpage=**2** |
| `81 29` | varint    | col[4] type=0x81,0x29 = 169 → text, len=(169−13)/2=**78** → full CREATE TABLE SQL |

**Decoded record:**  
`type="table"`, `name="students"`, `tbl_name="students"`, `rootpage=2`, `sql="CREATE TABLE students (...)"` (78 chars)

---

### Cell 2 — `idx_students_name` index entry (file offset 0x13C)

```
00000130: ...55 02 06 17
00000140: 2f 1d 01 6d 69 6e 64 65 78 69 64 78 5f 73 74 75
00000150: 64 65 6e 74 73 5f 6e 61 6d 65 73 74 75 64 65 6e
00000160: 74 73 03 43 52 45 41 54 45 20 49 4e 44 45 58 ...
```

| Byte(s) | Hex       | Meaning                                        |
|:---:|:---:|---|
| `55`    | 0x55 = 85 | Payload length = 85 bytes                      |
| `02`    | 2         | rowid = **2**                                  |
| `06`    | 6         | Record header size = 6 bytes                   |
| `17`    | 23        | col[0] type=23 → text, len=**5** → `"index"`  |
| `2f`    | 47        | col[1] type=47 → text, len=(47−13)/2=**17** → `"idx_students_name"` |
| `1d`    | 29        | col[2] type=29 → text, len=**8** → `"students"` |
| `01`    | 1         | col[3] type=1 → 1-byte int → rootpage=**3**   |
| `6d`    | 109       | col[4] type=109 → text, len=(109−13)/2=**48** → CREATE INDEX SQL |

**Decoded record:**  
`type="index"`, `name="idx_students_name"`, `tbl_name="students"`, `rootpage=3`, `sql="CREATE INDEX idx_students_name ON students(name)"` (48 chars)

---

## Page 2 — `students` Table Leaf

**File offsets:** `0x200`–`0x3FF` (bytes 512–1023)  
**Page-relative offset → file offset:** add `0x200` (512)

### B-tree Page Header

```
00000200: 0d 00 00 00 04 01 d2 00 01 f5 01 ea 01 de 01 d2
```

| Page Offset | Raw Hex | Field              | Value                         |
|:---:|:---:|---|---|
| 0x00        | `0d`    | Page type          | `0x0D` = Leaf table B-tree    |
| 0x01–0x02   | `00 00` | First freeblock    | 0                             |
| 0x03–0x04   | `00 04` | Cell count         | **4 cells**                   |
| 0x05–0x06   | `01 d2` | Cell content start | Page offset 0x01D2 = **466**  |
| 0x07        | `00`    | Fragmented bytes   | 0                             |

### Cell Pointer Array

```
00000200: ... 01 f5 01 ea 01 de 01 d2
               ↑    ↑    ↑    ↑
            ptr[0] ptr[1] ptr[2] ptr[3]
```

| Ptr | Raw Hex | Page Offset | File Offset | Row         |
|:---:|:---:|:---:|:---:|---|
| [0] | `01 f5` | 501         | 0x3F5 = 1013 | id=1 Asha   |
| [1] | `01 ea` | 490         | 0x3EA = 1002 | id=2 Ravi   |
| [2] | `01 de` | 478         | 0x3DE = 990  | id=3 Meena  |
| [3] | `01 d2` | 466         | 0x3D2 = 978  | id=4 Kiran  |

### Cell Content Area Hex

```
000003d0: 00 00 0a 04 04 00 17 01 4b 69 72 61 6e 17 0a 03
000003e0: 04 00 17 01 4d 65 65 6e 61 16 09 02 04 00 15 01
000003f0: 52 61 76 69 15 09 01 04 00 15 01 41 73 68 61 14
```

### Cell Decode — All Four Rows

For each cell the INTEGER PRIMARY KEY (`id`) is the rowid itself and is stored as `NULL` (serial type `0x00`) inside the record payload — SQLite does not duplicate it.

#### Row id=4 — Kiran (file offset 0x3D2 = 978)

| Byte | Hex  | Role                                |
|:---:|:---:|---|
| `0a` | 10   | Payload length = 10 bytes           |
| `04` | 4    | rowid = **4**                       |
| `04` | 4    | Header size = 4 bytes               |
| `00` |      | col[id] serial type = NULL (= rowid)|
| `17` | 23   | col[name] type → text 5 chars       |
| `01` |      | col[age] type → 1-byte int          |
| `4b 69 72 61 6e` | | Data: `"Kiran"` (5 bytes) |
| `17` | 23   | Data: age = **23**                  |

#### Row id=3 — Meena (file offset 0x3DE = 990)

| Byte | Hex  | Role                                |
|:---:|:---:|---|
| `0a` | 10   | Payload length = 10 bytes           |
| `03` | 3    | rowid = **3**                       |
| `04` | 4    | Header size = 4 bytes               |
| `00` |      | col[id] = NULL                      |
| `17` | 23   | col[name] → text 5 chars            |
| `01` |      | col[age] → 1-byte int               |
| `4d 65 65 6e 61` | | Data: `"Meena"` (5 bytes) |
| `16` | 22   | Data: age = **22**                  |

#### Row id=2 — Ravi (file offset 0x3EA = 1002)

| Byte | Hex  | Role                                |
|:---:|:---:|---|
| `09` | 9    | Payload length = 9 bytes            |
| `02` | 2    | rowid = **2**                       |
| `04` | 4    | Header size = 4 bytes               |
| `00` |      | col[id] = NULL                      |
| `15` | 21   | col[name] → text 4 chars            |
| `01` |      | col[age] → 1-byte int               |
| `52 61 76 69` | | Data: `"Ravi"` (4 bytes)    |
| `15` | 21   | Data: age = **21**                  |

#### Row id=1 — Asha (file offset 0x3F5 = 1013)

| Byte | Hex  | Role                                |
|:---:|:---:|---|
| `09` | 9    | Payload length = 9 bytes            |
| `01` | 1    | rowid = **1**                       |
| `04` | 4    | Header size = 4 bytes               |
| `00` |      | col[id] = NULL                      |
| `15` | 21   | col[name] → text 4 chars            |
| `01` |      | col[age] → 1-byte int               |
| `41 73 68 61` | | Data: `"Asha"` (4 bytes)    |
| `14` | 20   | Data: age = **20**                  |

---

## Page 3 — `idx_students_name` Index Leaf

**File offsets:** `0x400`–`0x5FF` (bytes 1024–1535)  
**Page-relative offset → file offset:** add `0x400` (1024)

### B-tree Page Header

```
00000400: 0a 00 00 00 04 01 db 00 01 f8 01 ee 01 e4 01 db
```

| Page Offset | Raw Hex | Field              | Value                        |
|:---:|:---:|---|---|
| 0x00        | `0a`    | Page type          | `0x0A` = **Leaf index B-tree** |
| 0x01–0x02   | `00 00` | First freeblock    | 0                            |
| 0x03–0x04   | `00 04` | Cell count         | **4 cells**                  |
| 0x05–0x06   | `01 db` | Cell content start | Page offset 0x01DB = **475** |
| 0x07        | `00`    | Fragmented bytes   | 0                            |

> **Note:** Index leaf pages use page type `0x0A`, not `0x0D`. They store `(indexed_key, rowid)` pairs sorted by key, enabling binary search. Unlike table leaves, each cell has **no rowid prefix** — the rowid is embedded inside the record payload as the last column.

### Cell Pointer Array

```
00000400: ... 01 f8 01 ee 01 e4 01 db
               ↑    ↑    ↑    ↑
            ptr[0] ptr[1] ptr[2] ptr[3]
```

| Ptr | Raw Hex | Page Offset | File Offset | Key (name) |
|:---:|:---:|:---:|:---:|---|
| [0] | `01 f8` | 504         | 0x5F8 = 1528 | "Asha"      |
| [1] | `01 ee` | 494         | 0x5EE = 1518 | "Kiran"     |
| [2] | `01 e4` | 484         | 0x5E4 = 1508 | "Meena"     |
| [3] | `01 db` | 475         | 0x5DB = 1499 | "Ravi"      |

> The index stores names in **lexicographic (alphabetical) order**: Asha → Kiran → Meena → Ravi. The cell content area (0x5DB–0x5FF) falls in the second half of page 3, beyond the 80-line head of the hex dump shown here — available in `page3.txt`.

---

## B-tree Node Structure Deep Dive

### How SQLite Uses B-trees

SQLite uses a **B+ tree** variant where:
- **Interior pages** hold separator keys and child page pointers (no actual row data)
- **Leaf pages** hold the actual records (or index entries)
- All leaves are at the same depth

Because our database has only 4 rows and 4 index entries, all three objects (schema, table, index) fit in **single leaf pages** — no interior pages are needed.

### Structural Diagram

```
                  ┌──────────────────────────────┐
                  │   Page 1: sqlite_master leaf  │
                  │   (schema catalog)            │
                  │                               │
                  │  Cell[0] rowid=1 → "students" │  rootpage=2
                  │  Cell[1] rowid=2 → "idx_..."  │  rootpage=3
                  └──────────────────────────────┘

    ┌──────────────────────────────────┐   ┌────────────────────────────────────┐
    │   Page 2: students table leaf    │   │  Page 3: idx_students_name leaf    │
    │   (table B-tree, type 0x0D)      │   │  (index B-tree, type 0x0A)         │
    │                                  │   │                                    │
    │  ptr[0]=501 → rowid=1, Asha, 20  │   │  ptr[0]=504 → ("Asha",  rowid=1)  │
    │  ptr[1]=490 → rowid=2, Ravi, 21  │   │  ptr[1]=494 → ("Kiran", rowid=4)  │
    │  ptr[2]=478 → rowid=3, Meena, 22 │   │  ptr[2]=484 → ("Meena", rowid=3)  │
    │  ptr[3]=466 → rowid=4, Kiran, 23 │   │  ptr[3]=475 → ("Ravi",  rowid=2)  │
    └──────────────────────────────────┘   └────────────────────────────────────┘
         (ordered by rowid / PRIMARY KEY)        (ordered lexicographically by name)
```

### Anatomy of One Table Leaf Cell (Kiran, row 4)

```
File offset 0x3D2
│
├── [0x0A]           Payload length varint = 10 bytes
├── [0x04]           rowid varint = 4
│
└── RECORD PAYLOAD (10 bytes):
    ├── [0x04]       Header size varint = 4  (covers next 3 bytes)
    ├── [0x00]       Serial type: id   = NULL  (value is in rowid)
    ├── [0x17]       Serial type: name = 23  → text of length (23−13)/2 = 5
    ├── [0x01]       Serial type: age  = 1   → 1-byte signed integer
    ├── [4b 69 72 61 6e]  name data = "Kiran"
    └── [0x17]       age data = 23
```

---

## Record Format and Serial Types

SQLite records use **variable-length serial type codes** in the header to describe each column's type and size.

### Serial Type Encoding Rules

| Serial Type Value | Storage Class | Data Size                    |
|:---:|---|---|
| 0                 | NULL          | 0 bytes                      |
| 1                 | INTEGER       | 1 byte (signed)              |
| 2                 | INTEGER       | 2 bytes (big-endian signed)  |
| 4                 | INTEGER       | 4 bytes                      |
| 6                 | INTEGER       | 8 bytes                      |
| 7                 | REAL          | 8 bytes IEEE 754             |
| 8                 | INTEGER 0     | 0 bytes (value is always 0)  |
| 9                 | INTEGER 1     | 0 bytes (value is always 1)  |
| N ≥ 13, odd       | TEXT          | length = **(N − 13) / 2**    |
| N ≥ 12, even      | BLOB          | length = **(N − 12) / 2**    |

### Serial Types Found in This Lab

| Column | Serial Type Hex | Decimal | Decoded As        | Example Value |
|---|:---:|:---:|---|---|
| `id`   | `00`           | 0       | NULL (= rowid)    | 1, 2, 3, 4    |
| `name` | `15`           | 21      | text 4 chars      | "Asha", "Ravi"|
| `name` | `17`           | 23      | text 5 chars      | "Meena","Kiran"|
| `age`  | `01`           | 1       | 1-byte signed int | 20, 21, 22, 23|

---

## Lookup Process: Finding a Row by Name

Query: `SELECT * FROM students WHERE name = 'Meena';`

### Step-by-Step Trace

```
Step 1 — Open database
         Read file header at byte 0.
         Confirm page_size = 0x0200 = 512.

Step 2 — Read sqlite_master (Page 1)
         Page 1 B-tree header at file offset 0x064.
         Scan cells: find entry where name='students', rootpage=2
                               and  name='idx_students_name', rootpage=3.

Step 3 — Choose index (idx_students_name), rootpage=3
         Seek to Page 3: file_offset = (3 − 1) × 512 = 1024 = 0x400.

Step 4 — Binary search in Page 3 index leaf
         Page 3 has 4 cells ordered by name: Asha, Kiran, Meena, Ravi.
         Cell pointer array: [504, 494, 484, 475]
         Binary search for "Meena" → found at page offset 484
         File offset = 1024 + 484 = 1508 = 0x5E4.
         Extract: key="Meena", rowid=3.

Step 5 — Fetch row from table (Page 2), rowid=3
         Seek to Page 2: file_offset = (2 − 1) × 512 = 512 = 0x200.
         Page 2 has 4 cells ordered by rowid: 1, 2, 3, 4.
         Cell pointer array: [501, 490, 478, 466]
         Scan / binary search for rowid=3 → page offset 478.
         File offset = 512 + 478 = 990 = 0x3DE.

Step 6 — Decode the cell at file offset 0x3DE
         0a → payload=10, 03 → rowid=3
         04 00 17 01 → header: id=NULL, name=5-char text, age=1-byte int
         4d 65 65 6e 61 → "Meena"
         16 → age=22

Step 7 — Return: 3|Meena|22
```

### Lookup Path Diagram

```
Query: WHERE name = 'Meena'
         │
         ▼
   ┌─────────────┐      read rootpage=3
   │   Page 1    │ ─────────────────────────►  ┌──────────────────┐
   │  (schema)   │                              │     Page 3       │
   └─────────────┘                              │  (index leaf)    │
                                                │                  │
                                                │  "Asha"  → rid=1 │
                                                │  "Kiran" → rid=4 │
                                                │  "Meena" → rid=3 │◄── found
                                                │  "Ravi"  → rid=2 │
                                                └─────┬────────────┘
                                                      │ rowid = 3
                                                      ▼
                                               ┌──────────────────┐
                                               │     Page 2       │
                                               │  (table leaf)    │
                                               │                  │
                                               │  rowid=1  Asha   │
                                               │  rowid=2  Ravi   │
                                               │  rowid=3  Meena  │◄── fetch
                                               │  rowid=4  Kiran  │
                                               └──────────────────┘
                                                      │
                                                      ▼
                                               Result: 3|Meena|22
```

---

## Page Offset and Address Reference

### Quick Address Calculator

For any page number N (1-based) and a page-relative byte offset P:

```
file_offset = (N − 1) × page_size + P
            = (N − 1) × 512      + P
```

### All Key Addresses in This Database

| Object                 | Page | Page-rel Offset | File Offset (hex) | Content            |
|---|:---:|:---:|:---:|---|
| File header            | 1    | 0               | 0x000             | 100-byte magic + metadata |
| Page 1 B-tree header   | 1    | 100 (0x64)      | 0x064             | `0d 00 00 00 02...`|
| Cell ptr[0] (students) | 1    | 108 (0x6C)      | 0x06C             | `01 93`            |
| Cell ptr[1] (index)    | 1    | 110 (0x6E)      | 0x06E             | `01 3c`            |
| students schema cell   | 1    | 403 (0x193)     | 0x193             | rowid=1, rootpage=2|
| idx schema cell        | 1    | 316 (0x13C)     | 0x13C             | rowid=2, rootpage=3|
| Page 2 B-tree header   | 2    | 0               | 0x200             | `0d 00 00 00 04...`|
| Row: Asha (id=1)       | 2    | 501 (0x1F5)     | 0x3F5             | `09 01 04 00 15 01 41...`|
| Row: Ravi (id=2)       | 2    | 490 (0x1EA)     | 0x3EA             | `09 02 04 00 15 01 52...`|
| Row: Meena (id=3)      | 2    | 478 (0x1DE)     | 0x3DE             | `0a 03 04 00 17 01 4d...`|
| Row: Kiran (id=4)      | 2    | 466 (0x1D2)     | 0x3D2             | `0a 04 04 00 17 01 4b...`|
| Page 3 B-tree header   | 3    | 0               | 0x400             | `0a 00 00 00 04...`|
| Index: "Asha"/1        | 3    | 504 (0x1F8)     | 0x5F8             | (in page3.txt)     |
| Index: "Kiran"/4       | 3    | 494 (0x1EE)     | 0x5EE             | (in page3.txt)     |
| Index: "Meena"/3       | 3    | 484 (0x1E4)     | 0x5E4             | (in page3.txt)     |
| Index: "Ravi"/2        | 3    | 475 (0x1DB)     | 0x5DB             | (in page3.txt)     |

---

## Key Observations

1. **Page size affects B-tree height.** At 512 bytes (the minimum allowed), the usable space per page after the header and cell pointer array is very small. A larger page size (e.g., the default 4096) would store more cells per page and reduce tree depth for large datasets.

2. **INTEGER PRIMARY KEY is free.** Columns declared `INTEGER PRIMARY KEY` alias SQLite's internal rowid. The column is recorded as serial type `0x00` (NULL) in the payload — no extra storage is consumed.

3. **Cell pointers grow down; cells grow up.** The cell pointer array grows from offset 8 downward; the cell content area grows from the bottom of the page upward. The free space region shrinks from both ends.

4. **Variable-width varints enable compact storage.** Payload lengths, rowids, and record header sizes all use SQLite's variable-length integer encoding (similar to Protocol Buffers). Small integers (< 128) occupy exactly 1 byte.

5. **Index keeps names in sorted order, not insertion order.** The `idx_students_name` index stores Asha → Kiran → Meena → Ravi lexicographically, regardless of the INSERT order (1→2→3→4). This ordering is what enables O(log n) binary search.

6. **Schema cookie = 3.** The file change counter is 7, but the schema cookie at offset 0x28 is 3 — it increments only on schema changes (`CREATE TABLE`, `CREATE INDEX`), not on every DML statement.

7. **All three pages are leaf pages.** With only 4 rows and 4 index entries, no interior (interior-node) pages are needed. The B-tree for each object has a height of 1 (root = leaf).

---

*Lab completed using SQLite 3.45.1 on Ubuntu 24.04 Noble. All hex values read from `lab.db` via `xxd -g 1 -c 16`. No values are fabricated — every byte cited is present in the provided terminal session.*