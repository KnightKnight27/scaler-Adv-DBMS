# LAB 4: SQLite3 Hex Dump Analysis — B-Tree Internals

## Overview

This lab explores the **binary file format** of SQLite3 by creating a database, dumping its raw bytes with `xxd` (or PowerShell equivalents), and manually navigating the **B-Tree page structure**, cell pointers, and record layout.

---

## 1. Setup Commands

### Step 1: Create the Database
```
Students   : 15 rows
Enrollments: 90 rows
```

### Step 2: Generate Hex Dumps

Using `xxd` to dump the database to a text file for analysis:

```bash
xxd lab4_btree.db > hexdump.txt
```
---

## 2. SQLite File Format Overview

A SQLite database is a single file divided into fixed-size **pages** (default 4096 bytes). Every page is one of the following types:

| Page Type | Flag Byte | Description |
|-----------|-----------|-------------|
| Interior Table B-Tree | `0x05` | Non-leaf node of a table (rowid) B-tree |
| Leaf Table B-Tree | `0x0D` | Leaf node storing actual row data |
| Interior Index B-Tree | `0x02` | Non-leaf node of an index B-tree |
| Leaf Index B-Tree | `0x0A` | Leaf node of an index B-tree |

**Page numbering is 1-based.** Page 1 starts at file offset `0x0000`, page 2 at `0x1000` (4096), page 3 at `0x2000` (8192), etc.

```
File Offset = (PageNumber - 1) × PageSize
```

---

## 3. File Header (First 100 Bytes of Page 1)

The first 100 bytes of the file are the **database header**. Page 1 is special — it contains both the file header AND the `sqlite_master` table B-tree.

### Real Hex Dump — File Header

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 04 00 00 00 04  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 03 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
00000040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04  ................
00000060: 00 2e 76 8b                                      ..v.
```

### Field-by-Field Breakdown

| Offset | Size | Hex Value | Decoded | Field |
|--------|------|-----------|---------|-------|
| 0x00 | 16 | `53 51...33 00` | `"SQLite format 3\0"` | Magic string |
| 0x10 | 2 | `10 00` | **4096** | Page size in bytes |
| 0x12 | 1 | `01` | 1 | File format write version |
| 0x13 | 1 | `01` | 1 | File format read version |
| 0x14 | 1 | `00` | 0 | Reserved bytes per page |
| 0x18 | 4 | `00 00 00 04` | **4** | File change counter |
| 0x1C | 4 | `00 00 00 04` | **4** | Database size (pages) |
| 0x28 | 4 | `00 00 00 03` | 3 | Schema format number |
| 0x2C | 4 | `00 00 00 04` | 4 | Default page cache size |
| 0x38 | 4 | `00 00 00 01` | 1 | Text encoding (1=UTF-8) |
| 0x5C | 4 | `00 00 00 04` | 4 | SQLite version number |

**Key takeaway:** At offset `0x10`, the bytes `10 00` decode to 4096 — our page size. At `0x1C`, `00 00 00 04` tells us there are 4 pages total in `lab4_btree.db`.

---

## 4. Page 1 — The `sqlite_master` Table (B-Tree Root)

Page 1 stores the schema. After the 100-byte file header, the B-tree header begins at offset `0x64`:

```text
                                    ↓ B-tree header starts here (offset 0x64)
00000060: 00 2e 76 8b [0d] 00 00 00 03 0d e6 00 0f 5d 0e 56
00000070: 0d e6 00 00 ...
```

### B-Tree Page Header (8 bytes for leaf, 12 for interior)

| Offset | Size | Hex | Decoded | Field |
|--------|------|-----|---------|-------|
| 0x64 | 1 | `0D` | 13 | **Page type: Leaf Table B-Tree** |
| 0x65 | 2 | `00 00` | 0 | First freeblock offset |
| 0x67 | 2 | `00 03` | **3** | Number of cells (3 schema entries) |
| 0x69 | 2 | `0D E6` | 3558 | Start of cell content area |
| 0x6B | 1 | `00` | 0 | Fragmented free bytes |

### Cell Pointer Array (3 cells × 2 bytes each)

Immediately after the header:

```text
Pointer 1: 0F 5D → offset 0x0F5D within page
Pointer 2: 0E 56 → offset 0x0E56 within page
Pointer 3: 0D E6 → offset 0x0DE6 within page
```

These point to the 3 entries in `sqlite_master`:
1. **students** table (rootpage = **2**)
2. **enrollments** table (rootpage = **3**)
3. **idx_enrollments_student** index (rootpage = **4**)

---

## 5. Page 2 — Students Table (Leaf Table B-Tree)

**File offset: `0x1000`** (4096). This is the **root page** of the `students` table B-tree.

### Real Hex Dump — Page 2 Header

```text
00001000: 0d 00 00 00 0f 0e d3 00 0f eb 0f d8 0f c1 0f ac
00001010: 0f 99 0f 84 0f 6f 0f 5a 0f 46 0f 32 0f 25 0f 11
00001020: 0e fd 0e e9 0e d3 00 00
```

### B-Tree Header Decoded

| Offset | Size | Hex | Decoded | Meaning |
|--------|------|-----|---------|---------|
| 0x1000 | 1 | `0D` | 13 | **Leaf Table B-Tree** |
| 0x1001 | 2 | `00 00` | 0 | No freeblocks |
| 0x1003 | 2 | `00 0F` | **15** | **15 cells** (= 15 students) |
| 0x1005 | 2 | `0E D3` | 3795 | Cell content start offset |
| 0x1007 | 1 | `00` | 0 | Fragmented bytes |

### Cell Pointer Array (15 entries)

Each 2-byte pointer gives the offset within the page where a cell (row) begins:

```text
Cell  1 → 0x0FEB  (student id=1, Alice)
Cell  2 → 0x0FD8  (student id=2, Bob)
Cell  3 → 0x0FC1  (student id=3, Charlie)
...
Cell 15 → 0x0ED3  (student id=15, Olivia)
```

## 6. Page 3 — Enrollments Table (Leaf Table B-Tree)

**File offset: `0x2000`** (8192).

### Hex Dump — Page 3 Header

```text
00002000: 0d 00 00 00 5a 0a 93 00 0f f3 0f e5 0f d7 0f c8
00002010: 0f b8 0f a9 0f 9b 0f 8c 0f 7d 0f 6d 0f 5c 0f 4c
...
000020b0: 0a e2 0a d3 0a c4 0a b4 0a a3 0a 93 00 00 00 00
```

### Header Decoded

| Field | Hex | Value |
|-------|-----|-------|
| Page type | `0D` | Leaf Table B-Tree |
| Cell count | `00 5A` | **90 cells** (= 90 enrollment rows) |
| Content start | `0A 93` | Offset 2707 |

This is enrollment id=1, student_id=1 (`0x09` = constant 1), course="CS101", grade="A".

---

## 7. Page 4 — Index B-Tree (Leaf Index B-Tree)

**File offset: `0x3000`** (12288). This is the `idx_enrollments_student` index.

### Hex Dump — Page 4 Header

```text
00003000: 0a 00 00 00 5a 0d eb 00 0f fc 0f f7 0f f2 0f ed
00003010: 0f e8 0f e3 0f dd 0f d7 0f d1 0f cb 0f c5 0f bf
```

### Header Decoded

| Field | Hex | Value |
|-------|-----|-------|
| Page type | `0A` | **Leaf Index B-Tree** |
| Cell count | `00 5A` | **90 cells** (one per enrollment) |

### Index Cell Structure

Index B-tree cells are different from table cells — they have **no rowid prefix**. Instead, the cell contains the indexed column value(s) + the rowid of the original row.

At the end of page 4 (file offset `0x3FEB`):

```text
00003feb: 03 03 09 09
          ── ────── ──
          │  │      └─ rowid serial type (0x09 = integer 1)
          │  └─ header: 03 09 09
          └─ payload: 3 bytes
```

This index entry says: `student_id=1` (serial 0x09 = constant 1), pointing to enrollment rowid 1.

---

## 8. How a Lookup Works

### Example: `SELECT * FROM students WHERE id = 7`
Since `id` is the INTEGER PRIMARY KEY (= rowid), SQLite searches the **table B-tree** rooted at page 2:
1. **Read page 2** header → type `0x0D` (leaf), 15 cells.
2. **Binary search** cell pointer array for rowid = 7.
3. Cell pointer 7 → offset `0x0F6F`.
4. **Read cell** at file offset `0x1F6F` (decodes to Grace, 22, 3.8).

**Total I/O: 1 page read.**

### Example: `SELECT * FROM enrollments WHERE student_id = 3`
This uses the **index B-tree** on page 4:
1. **Read page 4** (index root) → type `0x0A` (leaf index), 90 cells.
2. **Binary search** for `student_id = 3`.
3. Find matching index cells → extract rowids (e.g., 13, 14, 15, 16, 17, 18).
4. **For each rowid**, go to page 3 (enrollments table) and look up the row.

**Total I/O: 2 page reads** (1 index page + 1 table page).

---