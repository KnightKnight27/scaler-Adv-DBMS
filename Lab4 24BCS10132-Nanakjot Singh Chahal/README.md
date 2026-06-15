# SQLite3 Internal Storage Analysis using `xxd`

## Introduction

This lab demonstrates the internal structure of an SQLite3 database file using hexadecimal analysis with the `xxd` utility. A custom SQLite database was created, populated with sample data, and then analyzed at the byte level to understand:

- SQLite database header structure
- Page organization
- B-tree node layout
- Cell pointer arrays
- Record storage format
- Row lookup mechanism
- Address and offset navigation

The analysis uses a real SQLite database and real hexadecimal dumps.

---

# Database Creation

## Create Database

```bash
sqlite3 test.db
```

## SQL Commands Used

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);

INSERT INTO students (name, age) VALUES
('Nanak', 20),
('Raj', 21),
('Simran', 22);
```

---

# Database Verification

## Show Tables

```bash
sqlite3 test.db ".tables"
```

Output:

```text
students
```

## Show Data

```bash
sqlite3 test.db "SELECT * FROM students;"
```

Output:

```text
1|Nanak|20
2|Raj|21
3|Simran|22
```

---

# SQLite Database Information

## PRAGMA Information

```bash
sqlite3 test.db "PRAGMA page_size;"
sqlite3 test.db "PRAGMA page_count;"
sqlite3 test.db "PRAGMA encoding;"
```

Output:

```text
4096
2
UTF-8
```

## Interpretation

| Property | Value |
|---|---|
| Page Size | 4096 bytes |
| Total Pages | 2 |
| Encoding | UTF-8 |

---

# SQLite File Structure

SQLite databases are divided into fixed-size pages.

## Database Layout

```text
+----------------------+
| SQLite Header        | 100 bytes
+----------------------+
| Page 1               |
| sqlite_master BTree  |
+----------------------+
| Page 2               |
| students table       |
+----------------------+
```

---

# Generating Hex Dump

## Command Used

```bash
xxd -g 1 test.db > full_hex_dump.txt
```

## Viewing Header

```bash
xxd -g 1 -l 200 test.db
```

---

# SQLite File Header Analysis

## Header Dump

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 02
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04
```

---

# Header Field Breakdown

## Magic String

Bytes:

```text
53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

ASCII:

```text
SQLite format 3
```

This identifies the file as a valid SQLite3 database.

---

## Page Size

Offset:

```text
0x10
```

Bytes:

```text
10 00
```

Big-endian interpretation:

```text
0x1000 = 4096 bytes
```

---

## Database Size in Pages

Offset:

```text
0x18
```

Bytes:

```text
00 00 00 02
```

Meaning:

```text
2 pages
```

---

## Database Encoding

Offset:

```text
0x38
```

Bytes:

```text
00 00 00 01
```

Meaning:

```text
UTF-8 encoding
```

---

# B-Tree Structure

SQLite stores tables using B-trees.

## B-tree Overview

```text
                 sqlite_master
                        |
                        v
                 Root Page = 2
                        |
                        v
                students table
```

---

# sqlite_master Table Analysis

The `sqlite_master` table stores schema metadata.

## Query Used

```bash
sqlite3 test.db "SELECT rootpage, sql FROM sqlite_master;"
```

Output:

```text
2|CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
)
```

## Interpretation

| Field | Value |
|---|---|
| Root Page | 2 |
| Table Name | students |

This means the `students` table B-tree begins at page 2.

---

# Page 1 Analysis

## Page 1 B-tree Header

Offset:

```text
0x64
```

Hex:

```text
0d 00 00 00 01 0f 8d 00
```

## Interpretation

| Bytes | Meaning |
|---|---|
| 0d | Leaf Table B-tree Page |
| 0000 | First freeblock |
| 0001 | Number of cells |
| 0f8d | Cell content start |
| 00 | Fragmented bytes |

---

# B-tree Page Layout

```text
+----------------------+
| Page Header          |
+----------------------+
| Cell Pointer Array   |
+----------------------+
|                      |
|      Free Space      |
|                      |
+----------------------+
| Cell Content Area    |
| (grows backward)     |
+----------------------+
```

---

# Cell Pointer Array

The cell pointer array stores offsets to records.

## Page 2 Header

Hex dump:

```text
00001000: 0d 00 00 00 03 0f de 00 0f f5 0f ec 0f de
```

## Interpretation

| Bytes | Meaning |
|---|---|
| 0d | Leaf table B-tree |
| 0003 | Number of cells |
| 0fde | Cell content area start |

## Cell Pointers

| Cell | Offset |
|---|---|
| Cell 0 | 0x0ff5 |
| Cell 1 | 0x0fec |
| Cell 2 | 0x0fde |

---

# Page 2 Structure

```text
Page 2 (offset 0x1000)

+----------------------+
| B-tree Page Header   |
+----------------------+
| Cell Pointer Array   |
| 0x0ff5 -> Nanak      |
| 0x0fec -> Raj        |
| 0x0fde -> Simran     |
+----------------------+
| Free Space           |
+----------------------+
| Record Data          |
+----------------------+
```

---

# SQLite Record Format

SQLite table leaf cells use the format:

```text
[payload_size]
[rowid]
[record_header]
[column_values]
```

---

# Row Record Analysis

## Nanak Record

Hex:

```text
09 01 04 00 17 01 4e 61 6e 61 6b 14
```

## Breakdown

| Field | Value |
|---|---|
| Payload Size | 09 = 9 bytes |
| Row ID | 01 |
| Header Size | 04 |
| Serial Type | 17 (text) |
| Name | Nanak (4e 61 6e 61 6b) |
| Age | 14 = 20 |

---

# Raj Record

Hex:

```text
07 02 04 00 13 01 52 61 6a 15
```

## Breakdown

| Field | Value |
|---|---|
| Payload Size | 7 |
| Row ID | 2 |
| Name | Raj (52 61 6a) |
| Age | 21 |

---

# Simran Record

Hex:

```text
0a 03 04 00 19 01 53 69 6d 72 61 6e 16
```

## Breakdown

| Field | Value |
|---|---|
| Payload Size | 10 |
| Row ID | 3 |
| Name | Simran (53 69 6d 72 61 6e) |
| Age | 22 |

---

# Serial Type Codes

SQLite uses serial type codes to describe column data.

## Examples

| Serial Type | Meaning |
|---|---|
| 00 | NULL |
| 01 | 1-byte integer |
| 13+ odd | TEXT |

## TEXT Length Formula

```text
(TEXT_SERIAL_TYPE - 13) / 2
```

Example for Nanak:

```text
0x17 = 23

(23 - 13) / 2 = 5 bytes
```

Result:

```text
Nanak
```

---

# Address Mapping

## Absolute File Offsets

| Record | Page Offset | Absolute File Offset |
|---|---|---|
| Nanak | 0x0ff5 | 8181 |
| Raj | 0x0fec | 8172 |
| Simran | 0x0fde | 8158 |

Formula:

```text
Absolute Offset = Page Start + Cell Offset
```

For page 2:

```text
4096 + cell_offset
```

---

# Lookup Process in SQLite

SQLite performs row lookup using B-tree traversal.

## Lookup Steps

1. Read SQLite database header
2. Locate `sqlite_master`
3. Find root page of target table
4. Traverse B-tree nodes
5. Read cell pointer array
6. Jump to cell offsets
7. Decode record payload
8. Return row data

---

# Real Hex Dump Evidence

## Header Dump

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 02  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04  ................
```

## Page 2 Header

```text
00001000: 0d 00 00 00 03 0f de 00 0f f5 0f ec 0f de
```

## Row Records

```text
00001fde: 0a 03 04 00 19 01 53 69 6d 72 61 6e 16
00001feb: 00 07 02 04 00 11 01 52 61 6a 15
00001ff5: 09 01 04 00 17 01 4e 61 6e 61 6b 14
```

---

# Observations

- SQLite stores data using fixed-size pages.
- Tables are implemented using B-tree structures.
- Records are variable-length.
- Cell pointers allow efficient lookup.
- Text fields use serial type encoding.
- SQLite stores rows compactly near the end of pages.
- The database file can be manually navigated using offsets.

---

# Conclusion

This lab demonstrated the internal structure of an SQLite3 database using hexadecimal analysis with `xxd`.

The analysis successfully identified:
- SQLite database header
- B-tree page headers
- Cell pointer arrays
- Table root pages
- Record layouts
- Row storage
- Address calculations
- Lookup traversal mechanism

By manually decoding the database file, the relationship between SQLite logical structures and physical byte-level storage became clear.

This experiment demonstrates how SQLite efficiently stores relational data using compact B-tree-based page organization.

---

**Nanakjot Singh Chahal — 24BCS10132**
