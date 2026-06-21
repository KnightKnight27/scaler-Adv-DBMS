# Lab 4: SQLite3 Database Internal Structure Analysis Using XXD

**Student:** Pranav Nayal  
**Roll No.:** 24BCS10236

---

## Objective

Explore the internal binary layout of a SQLite3 database file using `xxd`.  
Map logical database objects (tables, auto-indexes, schema catalog) to their physical byte representation on disk, and trace how the B-Tree page structure stores records.

---

## Database Under Analysis

The `students.db` file already present in this repository was used.  
It holds a `students` table with two unique-constrained columns, causing SQLite to auto-create two internal indexes.

The schema (reconstructed from the page-1 schema catalog payload):

```sql
CREATE TABLE students (
    id         INTEGER PRIMARY KEY,
    first_name TEXT,
    last_name  TEXT,
    age        INTEGER,
    email      TEXT UNIQUE,
    course     VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

Two UNIQUE columns (`roll_number` / `email`) generate two auto-indexes:
- `sqlite_autoindex_students_1`
- `sqlite_autoindex_students_2`

---

## File-Level Metadata

```
$ xxd -l 32 students.db
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0002 0000 0004  .....@  ........
```

| Field | Bytes (hex) | Decoded |
|---|---|---|
| Magic string | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | `SQLite format 3\0` |
| Page size | `10 00` (bytes 16-17) | 0x1000 = **4096 bytes** |
| File change counter | `00 00 00 02` (bytes 24-27) | 2 |
| Page count | `00 00 00 04` (bytes 28-31) | **4 pages** (16 384 bytes total) |

---

## High-Level Page Map

| Page | Absolute offset | Type | Contents |
|---|---|---|---|
| 1 | `0x0000` | Table Leaf (`0x0D`) | `sqlite_schema` catalog — 3 cells |
| 2 | `0x1000` | Table Leaf (`0x0D`) | `students` data rows — 2 cells |
| 3 | `0x2000` | Index Leaf (`0x0A`) | `sqlite_autoindex_students_2` — 2 cells |
| 4 | `0x3000` | Index Leaf (`0x0A`) | `sqlite_autoindex_students_1` — 2 cells |

---

## Page 1 — sqlite_schema Catalog

Because Page 1 carries the 100-byte file header, its B-Tree page header starts at offset **0x0064**.

```
$ xxd -s 96 -l 20 students.db
00000060: 002e 574a 0d0f f800 030e 7700 0e77 0fc7
00000070: 0f96
```

### B-Tree Page Header Decode (offset 0x64, 8 bytes)

| Byte offset | Hex | Meaning |
|---|---|---|
| `0x0064` | `0d` | **Page type = Table Leaf** |
| `0x0065–0x0066` | `0f f8` | First freeblock at page-relative offset 0x0FF8 = 4088 |
| `0x0067–0x0068` | `00 03` | **3 cells** (3 schema objects stored on this page) |
| `0x0069–0x006A` | `0e 77` | Cell-content area starts at page-relative offset 0x0E77 = 3703 |
| `0x006B` | `00` | Fragmented free bytes = 0 |

### Cell Pointer Array (3 × 2 bytes, starting at 0x006C)

```
0x006C–0x006D : 0e 77  →  page-relative offset 3703  (schema entry 1)
0x006E–0x006F : 0f c7  →  page-relative offset 4039  (schema entry 2)
0x0070–0x0071 : 0f 96  →  page-relative offset 3990  (schema entry 3)
```

These three cells correspond to the `students` table definition and its two auto-indexes.

### Schema Payload Inspection

Dumping near the cell-content area (`xxd -s 0xf9c -l 100 students.db`) reveals readable strings:

```
00000f9c: 696e 6465 7873 716c 6974 655f 6175 746f  indexsqlite_auto
00000fac: 696e 6465 785f 7374 7564 656e 7473 5f32  index_students_2
00000fcc: 696e 6465 7873 716c 6974 655f 6175 746f  indexsqlite_auto
00000fdc: 696e 6465 785f 7374 7564 656e 7473 5f31  index_students_1
```

The schema page carries the two auto-index names embedded as inline text payloads, confirming the three schema objects.

---

## Page 2 — students Data Leaf

```
$ xxd -s 0x1000 -l 12 students.db
00001000: 0d00 0000 020f 6700 0fb4 0f67
```

### B-Tree Page Header (offset 0x1000, 8 bytes)

| Field | Value | Meaning |
|---|---|---|
| Page type | `0d` | Table Leaf |
| Freeblock | `00 00` | No freeblocks (compacted) |
| Cell count | `00 02` | **2 rows** |
| Content offset | `0f 67` = 3943 | Payloads packed from bottom of page |

### Cell Pointer Array

```
0x1008–0x1009 : 0f b4  →  page-relative 4020  (row 1, rowid=1)
0x100A–0x100B : 0f 67  →  page-relative 3943  (row 2, rowid=2)
```

### Row Record Analysis

**Row at 0x1FB4 (rowid = 1, KartikBhatia):**

```
$ xxd -s 0x1fb4 -l 64 students.db
00001fb4: 4a01 0800 1919 0131 2d33 4b61 7274 696b
00001fc4: 4268 6174 6961 166b 6172 7469 6b40 6578
00001fd4: 616d 706c 652e 636f 6d43 6f6d 7075 7465
00001fe4: 7220 5363 6965 6e63 6532 3032 362d 3035
00001ff4: 2d31 3320 3231 3a32 373a 3131
```

Record decode:

| Byte(s) | Value | Meaning |
|---|---|---|
| `4a` | 74 | Payload length (varint) |
| `01` | 1 | Rowid = **1** |
| `08` | 8 | Header length (8 bytes including this byte) |
| `00` | 0 | Column 0 (id) = NULL — stored as rowid |
| `19` | 25 | Column 1 serial type: TEXT 6 bytes (`first_name`) |
| `19` | 25 | Column 2 serial type: TEXT 6 bytes (`last_name`) |
| `01` | 1 | Column 3 serial type: 1-byte integer (`age`) |
| `31` | 49 | Column 4 serial type: TEXT 18 bytes (`email`) |
| `2d` | 45 | Column 5 serial type: TEXT 16 bytes (`course`) |
| `33` | 51 | Column 6 serial type: TEXT 19 bytes (`created_at`) |
| data | — | `Kartik` · `Bhatia` · `22` · `kartik@example.com` · `Computer Science` · `2026-05-13 21:27:11` |

The serial type encoding formula for TEXT:  
`serial_type = (byte_length × 2) + 13`  
Example: `"Kartik"` is 6 bytes → `6×2+13 = 25 = 0x19` ✓

**Row at 0x1F67 (rowid = 2, PrashansaSharma):**

```
$ xxd -s 0x1f67 -l 56 students.db
00001f67: 4b02 0800 1f19 0137 2333 50 72 61 73 68
          61 6e 73 61 53 68 61 72 6d 61 15 70 72
          61 73 68 61 6e 73 61 40 65 78 61 6d 70
          6c 65 2e 63 6f 6d 45 6c 65 63 74 72 6f
          6e 69 63 73 32 30 32 36 2d 30 35 2d 31
          33 20 32 31 3a 32 37 3a 31 31
```

Decoded:
- `first_name`: `Prashansa` (9 bytes, type `0x1f = 31 = 9×2+13`)
- `last_name`: `Sharma` (6 bytes, type `0x19 = 25`)
- `age`: `0x15 = 21`
- `email`: `prashansa@example.com` (21 bytes, type `0x37 = 55 = 21×2+13`)
- `course`: `Electronics` (11 bytes, type `0x23 = 35 = 11×2+13`)
- `created_at`: `2026-05-13 21:27:11` (19 bytes, type `0x33 = 51`)

---

## Pages 3 & 4 — Auto-Index Leaves

```
$ xxd -s 0x2000 -l 12 students.db
00002000: 0a00 0000 020f f700 0ffc 0ff7

$ xxd -s 0x3000 -l 12 students.db
00003000: 0a00 0000 020f d000 0fea 0fd0
```

Both pages carry `page type = 0x0A` (Index Leaf) and 2 cells each — one index entry per student row. SQLite uses these B-Tree index pages to enforce UNIQUE constraints at O(log n) cost.

---

## Physical File Layout Summary

| Byte Range (hex) | Size | Component | Notes |
|---|---|---|---|
| `0x0000 – 0x0063` | 100 B | **Global file header** | Magic string, page size, page count, version fields |
| `0x0064 – 0x006B` | 8 B | **Page 1 B-Tree header** | Type 0x0D (Table Leaf), 3 cells, content at 0x0E77 |
| `0x006C – 0x0071` | 6 B | **Cell pointer array** | Offsets 3703, 4039, 3990 |
| `0x0072 – 0x0E76` | ~3.5 KB | **Free space** | Header grows ↓, payloads grow ↑ from page bottom |
| `0x0E77 – 0x0FFF` | 393 B | **Schema payloads** | CREATE TABLE statement + two index name records |
| `0x1000 – 0x1FFF` | 4096 B | **Page 2 — students data** | 2 table rows (KartikBhatia, PrashansaSharma) |
| `0x2000 – 0x2FFF` | 4096 B | **Page 3 — auto-index 2** | Index entries for UNIQUE constraint 2 |
| `0x3000 – 0x3FFF` | 4096 B | **Page 4 — auto-index 1** | Index entries for UNIQUE constraint 1 |

---

## Key Takeaways

1. **File header is 100 bytes**, embedded in Page 1, not a separate allocation.
2. **Page type byte** (`0x0D` = table leaf, `0x0A` = index leaf) determines how the engine interprets cell contents.
3. **Cell pointers grow downward** from just below the B-Tree header; **payloads grow upward** from the page bottom — the gap between them is free space.
4. **TEXT serial type** encodes length: `type = (N × 2) + 13`, so the byte count is recoverable without a schema scan.
5. **UNIQUE columns** silently add auto-index pages (one per constraint), visible as extra pages of type `0x0A` in the page map.
6. **SQLite's schema catalog** (the `sqlite_schema` table on Page 1) is itself a B-Tree; every table and index in the database is just a row in that catalog.
