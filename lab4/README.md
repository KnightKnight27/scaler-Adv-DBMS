# Lab 4: SQLite3 Database Internal Structure Analysis Using XXD

**Student:** Lokendra Singh Rajawat — 23bcs10075  
**Subject:** Advanced Database Management Systems

---

## Objective

To explore and analyze the internal storage structure of a SQLite3 database file using hexadecimal inspection tools (`xxd`, `strings`). Students investigate how SQLite stores metadata, schema definitions, B-tree pages, records, and page headers directly within the database file.

---

## Tools Used

| Tool      | Purpose                                        |
|-----------|------------------------------------------------|
| `sqlite3` | Create database, insert records, run PRAGMAs  |
| `xxd`     | Hex dump of binary database file               |
| `strings` | Extract printable ASCII strings from binary    |
| `bash`    | Automate all steps via shell scripts           |

---

## How to Run

```bash
cd lab4
bash create_db.sh    # Creates lab4_students.db with 10 records
bash inspect.sh      # Hex-inspects the database file
```

---

## Task 1: Database Creation

A SQLite database `lab4_students.db` was created with the following schema:

```sql
CREATE TABLE students (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    name    TEXT    NOT NULL,
    roll_no TEXT    NOT NULL UNIQUE,
    branch  TEXT    NOT NULL,
    cgpa    REAL    NOT NULL
);
```

**10 records** were inserted. The file was created immediately on disk.

---

## Task 2: Database Metadata Analysis

```
Page size (bytes) : 4096
Page count        : 4
Database size     : 16384 bytes (16 KB)
Encoding          : UTF-8
SQLite version    : 3.51.0
```

**sqlite_master contents:**

| type  | name                        | rootpage | sql                        |
|-------|-----------------------------|----------|----------------------------|
| table | students                    | 2        | CREATE TABLE students (…)  |
| index | sqlite_autoindex_students_1 | 3        | *(auto-created for UNIQUE)* |

**Observation:** SQLite uses root pages to locate B-tree structures. The `students` table lives on page 2; its unique index lives on page 3.

---

## Task 3: SQLite File Header Inspection

The first 16 bytes are the **magic signature** that identifies any valid SQLite file:

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
```

| Hex Value  | ASCII         | Meaning                      |
|------------|---------------|------------------------------|
| `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | `SQLite format 3\0` | File magic signature (16 B) |

**Full header fields (bytes 0–99):**

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0c40 2020 0000 0002 0000 0004  .....@  ........
00000020: 0000 0000 0000 0000 0000 0001 0000 0004  ................
...
```

| Offset | Size | Value   | Field                        |
|--------|------|---------|------------------------------|
| 0x00   | 16 B | `SQLite format 3\0` | Magic header string |
| 0x10   | 2 B  | `0x10 0x00` → 4096 | Page size          |
| 0x12   | 1 B  | `0x01`  | File format write version    |
| 0x13   | 1 B  | `0x01`  | File format read version     |
| 0x14   | 1 B  | `0x00`  | Reserved space per page      |
| 0x20   | 4 B  | `0x04`  | Total number of pages        |

---

## Task 4: B-Tree Page Header Analysis

The B-tree page header begins at byte **100** (0x64), immediately after the file header:

```
00000064: 0d0f ec00 030e 8300 0ed5 0fbb  ............
```

| Offset | Value | Meaning                                    |
|--------|-------|--------------------------------------------|
| 0x64   | `0x0D` | Page type: **Leaf Table B-tree** page     |
| 0x65–66 | `0x0FEC` | First freeblock offset: 4076           |
| 0x67–68 | `0x0003` | **3 cells** on this page (schema rows) |
| 0x69–6A | `0x0E83` | Cell content area starts at offset 3715|
| 0x6B   | `0x00` | Fragmented free bytes: 0                  |

> **Note:** Page type `0x0D` = Leaf table B-tree (stores actual records). `0x05` would be an interior page (stores only keys + child pointers).

---

## Task 5: Cell Pointer Array Examination

Immediately after the page header (at byte 108 = 0x6C):

```
0000006c: 0ed5 0fbb 0e83 ...
```

Each cell pointer is **2 bytes** (big-endian), pointing to the byte offset within the page where that record starts:

| Pointer # | Hex     | Decimal Offset | Points to    |
|-----------|---------|---------------|--------------|
| 1         | `0x0ED5` | 3797          | Record 1 (students table entry) |
| 2         | `0x0FBB` | 4027          | Record 2 (index entry)          |
| 3         | `0x0E83` | 3715          | Record 3 (schema CREATE entry)  |

---

## Task 6: Record Storage Analysis

Using `strings` to search raw binary for actual data:

```
Lokendra Singh Rajawat23bcs10075Computer Science@"333333
```

Raw hex showing the record payload for the first student:

```
00001fb0: 0000 0000 3e01 0600 3921 2d07 4c6f 6b65  ....>...9!-.Loke
00001fc0: 6e64 7261 2053 696e 6768 2052 616a 6177  ndra Singh Rajaw
00001fd0: 6174 3233 6263 7331 3030 3735 436f 6d70  at23bcs10075Comp
```

Observations:
- Text fields are stored as **UTF-8 encoded raw bytes** with no null terminator between fields
- REAL values (CGPA = 9.1) are stored as **IEEE 754 double-precision** (`40 22 33 33 33 33 33 33`)
- Records are stored **without delimiters** — SQLite uses a varint-encoded **record header** to specify field types and lengths

---

## Task 7: Schema Storage Analysis

The `CREATE TABLE` SQL is stored verbatim inside `sqlite_master` (page 1):

```
Searching for 'students' as ASCII in hex:
00000ee0: 6162 6c65 7374 7564 656e 7473 7374 7564  ablestudentsstud
00000f00: 4520 7374 7564 656e 7473 2028 0a20 2020  E students (.
```

This confirms that `sqlite_master` physically stores:
- Table name: `students`
- The full `CREATE TABLE students (...)` statement
- Root page number references

SQLite re-reads this schema on every connection to reconstruct the database structure.

---

## Task 8: Physical File Layout

```
Total pages        : 4
Page size          : 4096 bytes
File size on disk  : 16384 bytes (4 × 4096 = 16384 ✓)
```

| Page # | Offset      | Contents                                      |
|--------|-------------|-----------------------------------------------|
| 1      | 0x0000      | File header (100B) + sqlite_master B-tree root |
| 2      | 0x1000      | students table data (leaf B-tree page)         |
| 3      | 0x2000      | sqlite_autoindex_students_1 (unique index)      |
| 4      | 0x3000      | sqlite_sequence table (autoincrement counter)  |

---

## Key Observations

1. **Single-file architecture**: All data, metadata, schema, and indexes live in `lab4_students.db`.
2. **4096-byte pages**: SQLite organizes storage in fixed-size pages — the unit of I/O.
3. **B-tree structure**: Page type byte `0x0D` confirms a leaf-level B-tree node storing actual records.
4. **Cell pointer array**: Allows O(1) record location within a page — no sequential scan needed.
5. **Schema in-file**: The `CREATE TABLE` SQL is stored as a raw string inside the database file itself.
6. **Variable-length records**: SQLite uses varint-encoded headers to handle variable-length TEXT and BLOB fields efficiently.
7. **Verified magic**: `5351 4c69 7465 2066 6f72 6d61 7420 3300` matches `SQLite format 3\0` exactly.

---

## Analysis Questions

**Q1: What is the purpose of database pages?**  
Pages are the fundamental unit of I/O in SQLite. By organizing data into fixed-size pages (4096 bytes), SQLite minimizes disk seeks and aligns reads/writes to OS page boundaries, improving performance.

**Q2: How does SQLite store data differently from PostgreSQL?**  
SQLite packs everything (data, schema, indexes) into a single `.db` file using B-tree pages. PostgreSQL uses a multi-file structure with separate relation files, WAL files, and shared buffers managed by a background server.

**Q3: What is the purpose of the cell pointer array?**  
It enables O(1) access to any record on a page — SQLite reads the pointer at a known offset, then jumps directly to the record without scanning the entire page.

**Q4: Why is the SQLite magic string important?**  
The 16-byte magic string at the start of the file uniquely identifies it as a valid SQLite database. Tools like `file`, database drivers, and recovery utilities use this to detect and validate the file format.
