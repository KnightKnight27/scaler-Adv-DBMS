# LAB 4: SQLite3 Hex Dump Analysis — B-Tree Internals

## Overview
This lab explores the **binary file format** of SQLite3 by creating a database, dumping its raw bytes, and manually navigating the **B-Tree page structure**, cell pointers, and record layout.

---

## Files

| File | Purpose |
|------|---------|
| `setup.sql` | DDL + seed data (15 students, 90 enrollments) |
| `build_db.py` | Creates `lab4_btree.db` and writes `hexdump.txt` |
| `analyze.py` | Parses the binary file and prints B-tree internals |

---

## Quick Start

```bash
python build_db.py   # creates lab4_btree.db + hexdump.txt
python analyze.py    # prints page-by-page B-tree analysis
```

---

## 1. SQLite File Format Overview

A SQLite database is a single file divided into fixed-size **pages** (default 4096 bytes).

| Page Type | Flag Byte | Description |
|-----------|-----------|-------------|
| Interior Table B-Tree | `0x05` | Non-leaf node of a table B-tree |
| Leaf Table B-Tree | `0x0D` | Leaf node storing actual row data |
| Interior Index B-Tree | `0x02` | Non-leaf node of an index B-tree |
| Leaf Index B-Tree | `0x0A` | Leaf node of an index B-tree |

```
File Offset = (PageNumber - 1) × PageSize
```

---

## 2. File Header (First 100 Bytes of Page 1)

```
Offset  Size  Field
0x00    16    Magic string: "SQLite format 3\0"
0x10    2     Page size (0x10 0x00 → 4096)
0x12    1     File format write version
0x13    1     File format read version
0x18    4     File change counter
0x1C    4     Database size in pages (0x00 0x00 0x00 0x04 → 4)
0x28    4     Schema format number
0x38    4     Text encoding (1 = UTF-8)
```

---

## 3. Page Layout

### Page 1 — `sqlite_master` (Leaf Table B-Tree, `0x0D`)
B-tree header starts at offset **0x64** (after the 100-byte file header).

```
Offset  Hex    Decoded
0x64    0D     Page type: Leaf Table B-Tree
0x65    00 00  First freeblock: 0
0x67    00 03  Cell count: 3  (students, enrollments, index)
0x69    0D E6  Content area start: 3558
0x6B    00     Fragmented free bytes: 0
```

Cell pointer array (3 × 2 bytes):
```
0x0F5D → students table entry      (rootpage = 2)
0x0E56 → enrollments table entry   (rootpage = 3)
0x0DE6 → idx_enrollments_student   (rootpage = 4)
```

### Page 2 — `students` (Leaf Table B-Tree, `0x0D`)
File offset: `0x1000`

```
0x1000  0D     Leaf Table B-Tree
0x1003  00 0F  15 cells (= 15 students)
0x1005  0E D3  Content start: 3795
```

### Page 3 — `enrollments` (Leaf Table B-Tree, `0x0D`)
File offset: `0x2000`

```
0x2000  0D     Leaf Table B-Tree
0x2003  00 5A  90 cells (= 90 enrollment rows)
0x2005  0A 93  Content start: 2707
```

### Page 4 — `idx_enrollments_student` (Leaf Index B-Tree, `0x0A`)
File offset: `0x3000`

```
0x3000  0A     Leaf Index B-Tree
0x3003  00 5A  90 cells (one per enrollment)
0x3005  0D EB  Content start: 3563
```

---

## 4. Cell Record Format (Table Leaf)

Each cell in a table leaf page:
```
[varint: payload length] [varint: rowid] [payload]
  payload = [varint: header length] [serial types...] [column data...]
```

Serial type encoding:
```
0       → NULL
1–4     → integer (1–4 bytes)
5       → 6-byte integer
6       → 8-byte integer
7       → IEEE 754 float (8 bytes)
8,9     → constants 0 and 1
2n+12   → BLOB of length n
2n+13   → TEXT of length n
```

---

## 5. How a Lookup Works

### `SELECT * FROM students WHERE id = 7`
1. Read page 2 header → Leaf, 15 cells
2. Binary search cell pointer array for rowid = 7
3. Cell pointer → offset `0x0F6F`
4. Decode record → `(7, 'Grace', 22, 3.8)`

**I/O: 1 page read**

### `SELECT * FROM enrollments WHERE student_id = 3`
1. Read page 4 (index) → binary search for `student_id = 3`
2. Collect matching rowids (13–18)
3. Read page 3 (enrollments table) → fetch each row by rowid

**I/O: 2 page reads**
