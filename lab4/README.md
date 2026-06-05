# Lab 4: SQLite3 Database Internal Structure Analysis

**Roll No.:** 24BCS10318
**Name:** Utkarsh Raj

> Hexadecimal inspection of B-tree pages, record payloads, and schema metadata

---

## Objective

Explore and analyze the internal storage structure of a SQLite3 database file using hexadecimal inspection. This lab investigates how SQLite stores database metadata, schema definitions, B-tree pages, records, and page headers directly within the database file, providing practical exposure to low-level database storage concepts.

---

## Repository Structure

```
lab4/
├── README.md           ← this file
├── schema.sql          ← table definition + sample data
├── queries.sql         ← PRAGMA introspection queries
└── xxd.py              ← custom hex inspector (xxd unavailable on system)
```

---

## Part 1 — Database Creation

### Schema

```sql
CREATE TABLE students (
    id     INTEGER PRIMARY KEY AUTOINCREMENT,
    name   TEXT    NOT NULL,
    dept   TEXT    NOT NULL,
    grade  TEXT    NOT NULL,
    marks  INTEGER NOT NULL
);
```

### Inserted Records

| id | name            | dept             | grade | marks |
|----|-----------------|------------------|-------|-------|
| 1  | Alice Johnson   | Computer Science | A     | 92    |
| 2  | Bob Martinez    | Electronics      | B     | 78    |
| 3  | Carol Williams  | Mechanical       | A     | 88    |
| 4  | David Lee       | Computer Science | C     | 65    |
| 5  | Eva Chen        | Electronics      | A     | 95    |
| 6  | Frank Brown     | Civil            | B     | 74    |
| 7  | Grace Kim       | Computer Science | A     | 91    |
| 8  | Henry Adams     | Mechanical       | B     | 81    |

### Observations

- Table created with 5 columns: 1 `INTEGER PRIMARY KEY` (`AUTOINCREMENT`) and 4 data columns.
- 8 records inserted spanning 4 departments and 3 grade levels.
- `students.db` created on disk at **12,288 bytes**.
- SQLite automatically creates `sqlite_sequence` because `AUTOINCREMENT` is used.

---

## Part 2 — Database Metadata Analysis

### PRAGMA Results

| PRAGMA              | Value                    |
|---------------------|--------------------------|
| `page_size`         | 4096 bytes               |
| `page_count`        | 3                        |
| `encoding`          | UTF-8                    |
| `freelist_count`    | 0 (no free pages)        |
| `sqlite_master` rows| 2 (students, sqlite_sequence) |
| `students` rootpage | 2                        |
| `sqlite_sequence` rootpage | 3               |

### `sqlite_master` Catalog

```
students         │ 2 │ CREATE TABLE students (...)
sqlite_sequence  │ 3 │ CREATE TABLE sqlite_sequence(name,seq)
```

### Observations

- Page size is 4,096 bytes — the default for SQLite on modern systems.
- Three pages total: Page 1 = schema/catalog, Page 2 = students data, Page 3 = sequence counter.
- The `rootpage` column in `sqlite_master` points directly to the B-tree root page in the file.
- No free pages exist: every page is actively used.

---

## Part 3 — SQLite File Header Inspection

### xxd Output — bytes 0–99

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 03  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
00000040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02  ................
00000060: 00 2e 76 89                                      ..v.
```

### Parsed Header Fields

| Offset  | Field                  | Value            | Notes                                         |
|---------|------------------------|------------------|-----------------------------------------------|
| 0–15    | Magic string           | `SQLite format 3\0` | File type identifier                       |
| 16–17   | Page size              | `0x1000` = 4096  | Page granularity                              |
| 18      | Write format version   | 1                | Legacy/WAL-compatible                         |
| 19      | Read format version    | 1                | Must be ≤ write version                       |
| 20      | Reserved bytes/page    | 0                | No per-page reserved space                    |
| 21      | Max payload fraction   | 64               | Fixed; always `0x40`                          |
| 22      | Min payload fraction   | 32               | Fixed; always `0x20`                          |
| 23      | Leaf payload fraction  | 32               | Fixed; always `0x20`                          |
| 24–27   | File change counter    | 2                | Increments on each write                      |
| 28–31   | Page count             | 3                | Total pages in file                           |
| 32–35   | First freelist trunk   | 0                | No free pages                                 |
| 36–39   | Total free pages       | 0                | All pages in use                              |
| 40–43   | Schema cookie          | 1                | Schema version                                |
| 44–47   | Schema format          | 4                | Latest format number                          |
| 56–59   | Text encoding          | 1 = UTF-8        | Character encoding                            |
| 96–99   | SQLite version         | 3045001          | v3.45.1                                       |

### Observations

- The 16-byte magic string `SQLite format 3\0` immediately identifies the file as a valid SQLite3 database.
- Page size is stored at bytes 16–17 as a big-endian 16-bit integer (`0x1000` = 4096).
- The file change counter (bytes 24–27) is used by processes to detect concurrent writes without holding locks.
- Max/min/leaf payload fractions (bytes 21–23) are fixed constants defining when overflow pages are used.

---

## Part 4 — B-Tree Page Analysis

### xxd Output — Page 2 Header (bytes 4096–4127)

```
00001000: 0d 00 00 00 08 0e fa 00 0f d9 0f b8 0f 96 0f 73  ...............s
00001010: 0f 56 0f 3c 0f 19 0e fa 00 00 00 00 00 00 00 00  .V.<............
```

### Parsed B-Tree Page Header

| Byte(s)        | Field              | Hex      | Decoded Value                  |
|----------------|--------------------|----------|--------------------------------|
| 4096 (+0)      | Page type          | `0x0D`   | Leaf Table B-tree page         |
| 4097–4098 (+1) | First freeblock    | `0x0000` | 0 = no freeblocks              |
| 4099–4100 (+3) | Number of cells    | `0x0008` | 8 records on this page         |
| 4101–4102 (+5) | Cell content start | `0x0EFA` | Offset 3834 within page        |
| 4103 (+7)      | Fragmented bytes   | `0x00`   | 0 fragmented free bytes        |

### Page Type Reference

| Byte   | Meaning                        |
|--------|--------------------------------|
| `0x02` | Interior Index B-tree page     |
| `0x05` | Interior Table B-tree page     |
| `0x0A` | Leaf Index B-tree page         |
| `0x0D` | Leaf Table B-tree page ← this  |

### Free Space Calculation

| Item                  | Value                                  |
|-----------------------|----------------------------------------|
| Page size             | 4096 bytes                             |
| Page header size      | 8 bytes                                |
| Cell pointer array    | 8 cells × 2 bytes = 16 bytes           |
| Cell content area start | offset 3834 (from start of page)    |
| Free space between arrays | 3834 – 8 – 16 = **3810 bytes**   |
| Cell data packed from end | bytes 3834–4095 (262 bytes used)  |

### Observations

- Page type `0x0D` confirms a Leaf Table B-tree: it holds actual row data (no child page pointers).
- All 8 student records fit on a single leaf page — no interior B-tree nodes are needed.
- SQLite packs cells from the end of the page downward; the gap between cell pointers and cell data is unused free space.
- The absence of freeblocks (`0x0000`) means no rows have been deleted from this page.

---

## Part 5 — Cell Pointer Array Examination

### xxd Output — Cell Pointer Array (bytes 4104–4119)

```
00001008: 0f d9 0f b8 0f 96 0f 73 0f 56 0f 3c 0f 19 0e fa  .......s.V.<....
```

### Decoded Cell Pointers

| Ptr # | Raw Bytes | Page Offset | File Offset        | Row (approx.)   |
|-------|-----------|-------------|--------------------|-----------------|
| 1     | `0x0FD9`  | 4057        | `0x001FD9` (8153)  | Alice Johnson   |
| 2     | `0x0FB8`  | 4024        | `0x001FB8` (8120)  | Bob Martinez    |
| 3     | `0x0F96`  | 3990        | `0x001F96` (8086)  | Carol Williams  |
| 4     | `0x0F73`  | 3955        | `0x001F73` (8051)  | David Lee       |
| 5     | `0x0F56`  | 3926        | `0x001F56` (8022)  | Eva Chen        |
| 6     | `0x0F3C`  | 3900        | `0x001F3C` (7996)  | Frank Brown     |
| 7     | `0x0F19`  | 3865        | `0x001F19` (7961)  | Grace Kim       |
| 8     | `0x0EFA`  | 3834        | `0x001EFA` (7930)  | Henry Adams     |

### Observations

- Each cell pointer is a 2-byte big-endian integer giving the byte offset of a cell within its page.
- Pointers are stored in insertion (logical) order, but point to cells packed from high addresses downward.
- Cell pointer 1 (`0x0FD9` = 4057) is near the top of the page; pointer 8 (`0x0EFA` = 3834) is at the bottom — confirming top-down packing from the end of the page.
- Using the pointer array, SQLite can jump directly to any record in O(1) time without scanning the entire page.
- Total span of cell data: bytes 3834–4095 = **262 bytes** across all 8 rows.

---

## Part 6 — Record Storage Analysis

### Sample Cell Hex Dumps

**Cell #1 — Alice Johnson (file offset `0x001FD9`)**
```
00001fd9: 25 01 06 00 27 2d 0f 01 41 6c 69 63 65 20 4a 6f  %...'-.  Alice Jo
00001fe9: 68 6e 73 6f 6e 43 6f 6d 70 75 74 65 72 20 53 63  hnsonComputer Sc
00001ff9: 69 65 6e 63 65 41 5c 0d                          ienceA\.
```

**Cell #4 — David Lee (file offset `0x001F73`)**
```
00001f73: 21 04 06 00 1f 2d 0f 01 44 61 76 69 64 20 4c 65  !....-..David Le
00001f83: 65 43 6f 6d 70 75 74 65 72 20 53 63 69 65 6e 63  eComputer Scienc
00001f93: 65 43 41                                          eCA
```

**Cell #8 — Henry Adams (file offset `0x001EFA`)**
```
00001efa: 1d 08 06 00 23 21 0f 01 48 65 6e 72 79 20 41 64  ....#!..Henry Ad
00001f0a: 61 6d 73 4d 65 63 68 61 6e 69 63 61 6c 42 51     amsMechanicalBQ
```

### Cell Record Format

| Field            | Description                                                           |
|------------------|-----------------------------------------------------------------------|
| `payload_length` | Varint — total bytes of record data (not including itself)            |
| `row_id`         | Varint — INTEGER PRIMARY KEY value (= `id` column)                   |
| `header_length`  | Varint — byte count of the record header (including this field)       |
| `serial_types`   | One varint per column describing type and size of each value          |
| `column_data`    | Raw bytes for each column in column order                             |

### Decoded Records — All 8 Rows

| Row | Payload (B) | RowID | name            | dept             | grade | marks |
|-----|-------------|-------|-----------------|------------------|-------|-------|
| 1   | 37          | 1     | Alice Johnson   | Computer Science | A     | 92    |
| 2   | 31          | 2     | Bob Martinez    | Electronics      | B     | 78    |
| 3   | 32          | 3     | Carol Williams  | Mechanical       | A     | 88    |
| 4   | 33          | 4     | David Lee       | Computer Science | C     | 65    |
| 5   | 27          | 5     | Eva Chen        | Electronics      | A     | 95    |
| 6   | 24          | 6     | Frank Brown     | Civil            | B     | 74    |
| 7   | 33          | 7     | Grace Kim       | Computer Science | A     | 91    |
| 8   | 29          | 8     | Henry Adams     | Mechanical       | B     | 81    |

### Serial Type Encoding for Row 1

| Column | Value            | Serial Type    | Encoding Rule                                         |
|--------|------------------|----------------|-------------------------------------------------------|
| id     | 1                | — (rowid alias)| Not stored in record body; comes from cell header varint |
| name   | Alice Johnson (13 chars) | `0x27` = 39 | TEXT: (13×2)+13 = 39                         |
| dept   | Computer Science (16 chars) | `0x2D` = 45 | TEXT: (16×2)+13 = 45                      |
| grade  | A (1 char)       | `0x0F` = 15    | TEXT: (1×2)+13 = 15                                   |
| marks  | 92               | `0x01`         | INT8: 1-byte signed integer                           |

### Observations

- SQLite uses variable-length (varint) encoding for record headers, keeping overhead small for short integer values.
- `INTEGER PRIMARY KEY` is an alias for rowid; it is **never** stored as a physical column value, saving 1–8 bytes per row.
- TEXT column lengths are variable: a 1-char grade uses 1 byte; a 16-char department name uses 16 bytes. The serial type encodes both type and length.
- Payload sizes range from 24 bytes (Frank Brown) to 37 bytes (Alice Johnson), reflecting variable-length TEXT fields.
- All string data is stored in raw UTF-8 bytes with no null terminator; length is derived from the serial type.

---

## Part 7 — Schema Storage Analysis

### xxd Output — Page 1 (first 128 bytes)

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 03  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
00000040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02  ................
00000060: 00 2e 76 89 0d 00 00 00 02 0e d8 00 0f 2a 0e d8  ..v..........*..
00000070: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
```

### Page 1 B-Tree Header (at offset 100 / `0x64`)

| Field                   | Value                                       |
|-------------------------|---------------------------------------------|
| Byte 100 (`0x64`)       | `0x0D` — Leaf Table B-tree                  |
| Bytes 101–102           | `0x0000` — no freeblocks                    |
| Bytes 103–104           | `0x0002` — 2 schema rows                    |
| Bytes 105–106           | `0x0ED8` — cell content starts at offset 3800 |
| Byte 107                | `0x00` — no fragmented free bytes           |

### Decoded Schema Rows

**Schema Row #1 — `students` table**
```sql
type     = 'table'
name     = 'students'
tbl_name = 'students'
rootpage = 2
sql      = 'CREATE TABLE students (
    id     INTEGER PRIMARY KEY AUTOINCREMENT,
    name   TEXT NOT NULL,
    dept   TEXT NOT NULL,
    grade  TEXT NOT NULL,
    marks  INTEGER NOT NULL
)'
```

**Schema Row #2 — `sqlite_sequence` (internal)**
```sql
type     = 'table'
name     = 'sqlite_sequence'
tbl_name = 'sqlite_sequence'
rootpage = 3
sql      = 'CREATE TABLE sqlite_sequence(name,seq)'
```

### Schema Row Pointer Locations

| Ptr # | Page Offset     | File Offset     | Contents                                   |
|-------|-----------------|-----------------|--------------------------------------------|
| 1     | `0x0F2A` (3882) | `0x0F2A` (3882) | `students` table schema + CREATE TABLE SQL |
| 2     | `0x0ED8` (3800) | `0x0ED8` (3800) | `sqlite_sequence` schema (auto-generated)  |

### Observations

- Page 1 serves a dual role: its first 100 bytes are the database-wide file header, while bytes 100–4095 hold the `sqlite_schema` B-tree page.
- `sqlite_schema` (formerly `sqlite_master`) is a regular B-tree table whose rows happen to describe other tables.
- The full original `CREATE TABLE` statement is stored verbatim as a TEXT column. SQLite reconstructs table structure from this SQL at runtime.
- The `rootpage` column maps table names to page numbers, allowing O(1) lookup of any table's B-tree root.
- `sqlite_sequence` is automatically created when `AUTOINCREMENT` is first used.

---

## Part 8 — Physical File Layout

### File Overview

| Item          | Value                              |
|---------------|------------------------------------|
| Total size    | 12,288 bytes                       |
| Page size     | 4,096 bytes                        |
| Total pages   | 3                                  |
| Free pages    | 0                                  |
| File format   | SQLite 3 — single-file, self-contained |

### Page Boundary Hex Dumps

**Page 1 — offset `0x000000`**
```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 03  .....@  ........
```

**Page 2 — offset `0x001000` (4096)**
```
00001000: 0d 00 00 00 08 0e fa 00 0f d9 0f b8 0f 96 0f 73  ...............s
00001010: 0f 56 0f 3c 0f 19 0e fa 00 00 00 00 00 00 00 00  .V.<............
```

**Page 3 — offset `0x002000` (8192)**
```
00002000: 0d 00 00 00 01 0f f2 00 0f f2 00 00 00 00 00 00  ................
00002010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
```

### Annotated File Layout

| Byte Range      | Page | Contents                                              | Role                        |
|-----------------|------|-------------------------------------------------------|-----------------------------|
| 0 – 99          | 1    | 100-byte SQLite file header                           | Whole-database metadata     |
| 100 – 107       | 1    | B-tree page header for `sqlite_schema`                | Leaf table page header      |
| 108 – 111       | 1    | Cell pointer array (2 pointers × 2 bytes)             | Index into schema cells     |
| 112 – 3799      | 1    | Free space (unallocated)                              | Room for future tables      |
| 3800 – 4095     | 1    | Schema cells: `students` + `sqlite_sequence` definitions | Schema metadata storage  |
| 4096 – 4103     | 2    | B-tree page header (leaf, 8 cells, `0x0D`)            | `students` page header      |
| 4104 – 4119     | 2    | Cell pointer array (8 pointers × 2 bytes)             | Index into data cells       |
| 4120 – 7929     | 2    | Free space                                            | Room for more rows          |
| 7930 – 8191     | 2    | Row cells: 8 student records (packed from end)        | Actual user data            |
| 8192 – 8199     | 3    | B-tree page header (leaf, 1 cell)                     | `sqlite_sequence` header    |
| 8200 – 8201     | 3    | Cell pointer array (1 pointer)                        | Index to sequence cell      |
| 8202 – 12287    | 3    | Free space + sequence record at page end              | AUTOINCREMENT counter       |

### Observations

- SQLite stores everything — file metadata, schema, and user data — in a single file with no auxiliary control files.
- The 100-byte file header shares Page 1 with the `sqlite_schema` B-tree. Every other page starts immediately with a B-tree page header.
- Free space is tracked as the gap between the cell pointer array (growing down) and the cell content area (growing up from the end).
- Page 3 holds `sqlite_sequence` with a single row: `(students, 8)` — the highest `AUTOINCREMENT` value issued. Only 14 bytes of content exist on this 4096-byte page.
- The end-of-file region of Page 3 contains visible ASCII `students` in the hex dump, confirming the AUTOINCREMENT counter stores the table name as raw UTF-8.

---

## Conclusion

This lab provided a detailed, byte-level examination of a real SQLite3 database file. By applying hexadecimal inspection and custom varint decoding, the following design principles of the SQLite storage engine were verified:

1. **Self-describing file header** — The 100-byte header encodes all database-wide parameters (page size, encoding, version, page count) in a compact format.
2. **B-tree page headers** — Every page begins with an 8-byte header identifying its type (leaf/interior, table/index) and tracking cell count, free space, and fragmentation.
3. **O(1) record access** — Cell pointer arrays store 2-byte offsets that allow direct random access to any record on the page.
4. **Bidirectional packing** — Records pack from the high end of each page downward; the pointer array grows from the low end upward.
5. **Schema as data** — `sqlite_schema` stores the original `CREATE TABLE` SQL verbatim — SQLite's entire data dictionary is just a regular B-tree table.
6. **Rowid optimization** — `INTEGER PRIMARY KEY` columns are aliases for the implicit rowid and are never stored as physical column values, reducing record size.
7. **Variable-length serial types** — TEXT fields use encoding `(length×2+13)` that communicates both data type and exact byte size in the record header.
8. **Single-file portability** — The entire database (header, schema, user tables) coexists in one file, making SQLite databases fully self-contained.

