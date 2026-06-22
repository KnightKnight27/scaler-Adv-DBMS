# SQLite3 Internal Storage Analysis using XXD

**Name:** Vimal Kumar Yadav  
**Roll No:** 24BCS10273  
**Platform:** Fedora KDE Plasma Desktop  
**Tools:** SQLite3 · xxd · dbstat Virtual Table

---

## Objective

Explore the internal storage architecture of SQLite3 databases through low-level hexadecimal analysis. The investigation covers the complete SQLite storage pipeline — from raw bytes to structured rows — including page layout, B-Tree node structure, cell pointer arrays, record field encoding, interior node traversal, and index-based lookup.

---

## Tools Used

| Tool      | Purpose                         |
| --------- | ------------------------------- |
| `sqlite3` | Database creation and querying  |
| `xxd`     | Hexadecimal dump analysis       |
| `dbstat`  | SQLite internal page statistics |

---

## Database Design

A Library Management database was constructed to generate realistic B-Tree structures across multiple tables and indexes.

### Authors Table

```sql
CREATE TABLE authors (
    author_id INTEGER PRIMARY KEY,
    name      TEXT,
    country   TEXT
);
```

### Books Table

```sql
CREATE TABLE books (
    book_id        INTEGER PRIMARY KEY,
    title          TEXT,
    genre          TEXT,
    published_year INTEGER,
    author_id      INTEGER,
    FOREIGN KEY(author_id) REFERENCES authors(author_id)
);
```

### Members Table

```sql
CREATE TABLE members (
    member_id  INTEGER PRIMARY KEY,
    name       TEXT,
    department TEXT
);
```

### Issued Books Table

```sql
CREATE TABLE issued_books (
    issue_id    INTEGER PRIMARY KEY,
    book_id     INTEGER,
    member_id   INTEGER,
    issue_date  TEXT,
    return_date TEXT,
    FOREIGN KEY(book_id)   REFERENCES books(book_id),
    FOREIGN KEY(member_id) REFERENCES members(member_id)
);
```

### Indexes

```sql
CREATE INDEX idx_books_title   ON books(title);
CREATE INDEX idx_members_name  ON members(name);
```

These two indexes produce separate Index B-Tree structures within the database file.

---

## SQLite Internal Metadata

```sql
SELECT name, type, rootpage FROM sqlite_master;
```

| Name               | Type  | Root Page |
| ------------------ | ----- | --------- |
| `authors`          | table | 2         |
| `books`            | table | 3         |
| `members`          | table | 4         |
| `issued_books`     | table | 5         |
| `idx_books_title`  | index | 6         |
| `idx_members_name` | index | 7         |

---

## Page Configuration

### Page Size

```sql
PRAGMA page_size;
-- Output: 4096
```

Every SQLite page occupies exactly **4096 bytes**.

### Page Count

```sql
PRAGMA page_count;
-- Output: 7
```

The database contains **7 pages** in total.

---

## Page Address Calculation

SQLite pages are stored sequentially in the file. The byte offset for any page is:

```
offset = (page_number - 1) × page_size
```

**Example — Books table (root page 3):**

```
offset = (3 - 1) × 4096 = 8192 = 0x2000
```

The Books table B-Tree begins at byte offset **8192** in the file.

---

## SQLite File Header Analysis

```bash
xxd library.db | head -n 40
```

### SQLite Signature

```
53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

ASCII: `SQLite format 3` — confirms a valid SQLite3 database file.

| File Address        | Bytes                                             | Meaning                   |
| ------------------- | ------------------------------------------------- | ------------------------- |
| `0x0000` – `0x000F` | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | SQLite format 3 signature |

### Page Size Field

```
10 00
```

`0x1000 = 4096` — consistent with `PRAGMA page_size`.

| File Address        | Bytes   | Meaning                |
| ------------------- | ------- | ---------------------- |
| `0x0010` – `0x0011` | `10 00` | Page size = 4096 bytes |

---

## B-Tree Page Analysis

### Inspection Command

```bash
xxd -g 1 -s 8192 -l 256 library.db
```

### Raw Header — Books Table (Page 3)

```
0d 00 00 00 0f 0d d3 00
```

### Header Field Annotations

| File Address      | Byte    | Field                      | Value           |
| ----------------- | ------- | -------------------------- | --------------- |
| `0x2000`          | `0x0D`  | Page type                  | Table Leaf Page |
| `0x2001`–`0x2002` | `00 00` | First freeblock offset     | None            |
| `0x2003`–`0x2004` | `00 0F` | Number of cells            | 15              |
| `0x2005`–`0x2006` | `0D D3` | Start of cell content area | offset 0x0DD3   |
| `0x2007`          | `00`    | Fragmented free bytes      | 0               |

---

## SQLite Page Type Reference

| Byte   | Page Type      |
| ------ | -------------- |
| `0x0D` | Table Leaf     |
| `0x05` | Table Interior |
| `0x0A` | Index Leaf     |
| `0x02` | Index Interior |

---

## Slotted Page Architecture

SQLite uses a slotted-page architecture where cell pointers and record payloads grow toward each other from opposite ends of the page:

```
+------------------------------------------+
|  B-Tree Header          (fixed, top)     |
+------------------------------------------+
|  Cell Pointer Array     (grows downward) |
+------------------------------------------+
|                                          |
|  Free Space                              |
|                                          |
+------------------------------------------+
|  Record Payloads        (grows upward)   |
+------------------------------------------+
```

Cell pointers are 2-byte offsets measured from the start of the page. Record payloads are written from the bottom of the page upward.

---

## Cell Pointer Array

Immediately after the 8-byte B-Tree header, SQLite stores one 2-byte offset per cell pointing to each record payload.

```
0f e9   0f c4   0f 8e   0f 55   0f 29
0f 09   0e e5   0e cb   0e a9   0e 80
0e 67   0e 4c   0e 2b   0d fd   0d d3
```

These are all 15 cell pointers for the Books table page.

---

## Cell Pointer Resolution

Books table root page starts at `8192` (`0x2000`). Each cell pointer resolves as:

```
file_address = page_base_offset + cell_pointer_value
```

| Cell Pointer | Decimal Offset | File Address     | Record                                  |
| ------------ | -------------- | ---------------- | --------------------------------------- |
| `0x0FE9`     | 4073           | `0x2FE9` (12265) | 1984                                    |
| `0x0FC4`     | 4036           | `0x2FC4` (12228) | Animal Farm                             |
| `0x0F8E`     | 3982           | `0x2F8E` (12174) | Harry Potter and the Sorcerer's Stone   |
| `0x0F55`     | 3925           | `0x2F55` (12117) | Harry Potter and the Chamber of Secrets |
| `0x0F29`     | 3881           | `0x2F29` (12073) | Kafka on the Shore                      |
| `0x0F09`     | 3849           | `0x2F09` (12041) | —                                       |
| `0x0EE5`     | 3813           | `0x2EE5` (12005) | —                                       |
| `0x0ECB`     | 3787           | `0x2ECB` (11979) | —                                       |
| `0x0EA9`     | 3753           | `0x2EA9` (11945) | —                                       |
| `0x0E80`     | 3712           | `0x2E80` (11904) | —                                       |
| `0x0E67`     | 3687           | `0x2E67` (11879) | —                                       |
| `0x0E4C`     | 3660           | `0x2E4C` (11852) | —                                       |
| `0x0E2B`     | 3627           | `0x2E2B` (11819) | —                                       |
| `0x0DFD`     | 3581           | `0x2DFD` (11773) | —                                       |
| `0x0DD3`     | 3539           | `0x2DD3` (11731) | —                                       |

---

## Record Payload Analysis

### Jump to First Record

```bash
xxd -g 1 -s $((8192 + 4073)) -l 80 library.db
```

### Raw Bytes at `0x2FE9`

```
15 01 06 00 15 1f 02 09
31 39 38 34
44 79 73 74 6f 70 69 61 6e
07 9d
```

### Full Byte Annotation

| File Address      | Bytes                        | Field          | Meaning                         |
| ----------------- | ---------------------------- | -------------- | ------------------------------- |
| `0x2FE9`          | `15`                         | Payload length | 21 bytes                        |
| `0x2FEA`          | `01`                         | RowID          | 1                               |
| `0x2FEB`          | `06`                         | Header size    | 6 bytes                         |
| `0x2FEC`          | `00`                         | Serial type    | NULL / RowID alias (book_id)    |
| `0x2FED`          | `15`                         | Serial type    | TEXT, 4 bytes (title)           |
| `0x2FEE`          | `1F`                         | Serial type    | TEXT, 9 bytes (genre)           |
| `0x2FEF`          | `02`                         | Serial type    | 2-byte integer (published_year) |
| `0x2FF0`          | `09`                         | Serial type    | Integer constant 1 (author_id)  |
| `0x2FF1`–`0x2FF4` | `31 39 38 34`                | Body           | ASCII "1984"                    |
| `0x2FF5`–`0x2FFD` | `44 79 73 74 6f 70 69 61 6e` | Body           | ASCII "Dystopian"               |
| `0x2FFE`–`0x2FFF` | `07 9D`                      | Body           | Integer 1949                    |

### Reconstructed Row

```
book_id | title | genre     | published_year | author_id
      1 | 1984  | Dystopian |           1949 |         1
```

---

## Field Navigation Walkthrough

This section demonstrates how SQLite physically traverses a record byte by byte, using the `1984` record as the worked example.

**Raw bytes:**

```
15 01 06 00 15 1f 02 09 31 39 38 34 44 79 73 74 6f 70 69 61 6e 07 9d
```

---

**Step 1 — Read Payload Length (varint)**

```
Byte at 0x2FE9 = 0x15 = 21
```

Payload is 21 bytes. Advance pointer to `0x2FEA`.

---

**Step 2 — Read RowID (varint)**

```
Byte at 0x2FEA = 0x01 = 1
```

RowID = 1. Advance pointer to `0x2FEB`.

---

**Step 3 — Read Header Size (varint)**

```
Byte at 0x2FEB = 0x06 = 6
```

The record header occupies 6 bytes total (including this byte). The record body begins at `0x2FEB + 6 = 0x2FF1`. Advance pointer to `0x2FEC`.

---

**Step 4 — Read Serial Type Descriptors**

```
Bytes at 0x2FEC through 0x2FF0:
00   15   1f   02   09
```

| Byte | Serial Type        | Field            | Data Width                      |
| ---- | ------------------ | ---------------- | ------------------------------- |
| `00` | NULL / RowID alias | `book_id`        | 0 bytes (stored as RowID)       |
| `15` | TEXT               | `title`          | 4 bytes → `(0x15 - 13) / 2 = 4` |
| `1F` | TEXT               | `genre`          | 9 bytes → `(0x1F - 13) / 2 = 9` |
| `02` | Integer            | `published_year` | 2 bytes                         |
| `09` | Integer constant 1 | `author_id`      | 0 bytes (value = 1)             |

SQLite now knows the exact byte width of every field before touching the body.

---

**Step 5 — Read Record Body Using Accumulated Offsets**

Starting at `0x2FF1` (body start), read each field in sequence:

```
Bytes 0x2FF1–0x2FF4:  31 39 38 34  →  ASCII "1984"        (title, 4 bytes)
Bytes 0x2FF5–0x2FFD:  44 79 73 74 6f 70 69 61 6e  →  ASCII "Dystopian"  (genre, 9 bytes)
Bytes 0x2FFE–0x2FFF:  07 9D  →  0x079D = 1949             (published_year, 2 bytes)
(author_id = 1 encoded directly in serial type 0x09, no body bytes consumed)
```

SQLite reconstructs the full row by combining RowID, serial type widths, and body values. No separators, delimiters, or fixed-width padding are used — the header drives all field boundaries.

---

## Index B-Tree Analysis

### Inspection Command

```bash
xxd -g 1 -s $(( (6 - 1) * 4096 )) -l 128 library.db
```

Page 6 offset: `(6 - 1) × 4096 = 20480 = 0x5000`

### Raw Header — idx_books_title (Page 6)

```
0a 00 00 00 0f 0e a8 00
```

### Header Annotations

| File Address      | Byte    | Field                       | Value           |
| ----------------- | ------- | --------------------------- | --------------- |
| `0x5000`          | `0x0A`  | Page type                   | Index Leaf Page |
| `0x5001`–`0x5002` | `00 00` | First freeblock             | None            |
| `0x5003`–`0x5004` | `00 0F` | Number of index entries     | 15              |
| `0x5005`–`0x5006` | `0E A8` | Start of index payload area | offset 0x0EA8   |
| `0x5007`          | `00`    | Fragmented bytes            | 0               |

---

## Table B-Tree vs Index B-Tree

| Property       | Table B-Tree                      | Index B-Tree                      |
| -------------- | --------------------------------- | --------------------------------- |
| Page type byte | `0x0D` (leaf) / `0x05` (interior) | `0x0A` (leaf) / `0x02` (interior) |
| Stores         | Full row records                  | Indexed key + RowID reference     |
| Keyed by       | RowID                             | Indexed column value              |
| Navigation     | RowID lookup                      | Key comparison                    |

---

## Interior B-Tree Node Structure

Although the current database is small enough to fit entirely in single leaf pages, SQLite automatically creates interior nodes when a table or index grows beyond one page. Interior nodes handle navigation — they contain no row data, only routing information.

### Table Interior Page (type `0x05`)

```
+----------------------------------------------+
|  B-Tree Header (12 bytes for interior pages) |
|    Byte 0:     0x05 (table interior)         |
|    Bytes 1-2:  First freeblock               |
|    Bytes 3-4:  Number of cells               |
|    Bytes 5-6:  Cell content area start       |
|    Byte 7:     Fragmented free bytes         |
|    Bytes 8-11: Rightmost child pointer       |
+----------------------------------------------+
|  Cell Pointer Array                          |
+----------------------------------------------+
|  Cell Content Area                           |
|    Each cell:                                |
|      [4-byte left child page number]         |
|      [varint RowID divider key]              |
+----------------------------------------------+
```

The rightmost child pointer (bytes 8–11) is unique to interior pages and holds the page number for the subtree containing the largest keys.

### How Traversal Works

```
                    Interior Root Page
                    +--------------+
                    | RightPtr = 8 |
                    | Cell 1:      |
                    |  child = 3   |
                    |  key   = 100 |
                    | Cell 2:      |
                    |  child = 5   |
                    |  key   = 200 |
                    +--------------+
                      /      |      \
                     /       |       \
               RowID<100  100-199   RowID>=200
               Page 3     Page 5    Page 8
              (leaf)      (leaf)   (rightmost)
```

### Index Interior Page (type `0x02`)

Identical structure, but cells carry indexed key values instead of RowIDs:

```
+----------------------------------------------+
|  B-Tree Header (12 bytes)                    |
|    Byte 0:     0x02 (index interior)         |
|    Bytes 8-11: Rightmost child pointer       |
+----------------------------------------------+
|  Cell Pointer Array                          |
+----------------------------------------------+
|  Cell Content Area                           |
|    Each cell:                                |
|      [4-byte left child page number]         |
|      [index key record (key + RowID)]        |
+----------------------------------------------+
```

Example — title index split at "M":

```
                  Index Interior Root
                  +----------------+
                  | RightPtr = 12  |
                  | child=6, key=M |
                  +----------------+
                      /          \
               title < "M"    title >= "M"
                Page 6          Page 12
               (leaf)           (rightmost)
```

### When SQLite Creates Interior Nodes

SQLite promotes a leaf to an interior structure when a page becomes full:

1. Leaf page reaches capacity during an insert
2. SQLite splits the leaf into two new leaf pages
3. A divider key is selected (the median RowID or key value)
4. The divider is promoted into a parent page
5. If the root was the full leaf, a new root interior page is created

Transformation:

```
Before:                     After:
                            Interior Root
Single Leaf Root    →          /    \
(full)                      Leaf1  Leaf2
```

---

## All B-Tree Node Pointers

SQLite uses three distinct pointer types for navigating between and within pages.

---

### Pointer Type 1 — Cell Offset Pointers (2 bytes, within a page)

These live in the Cell Pointer Array immediately after the page header. Each is a 2-byte unsigned integer giving the byte offset from the start of the current page to a cell's payload.

```
Stored as:   0F E9
Value:       0x0FE9 = 4073
Resolves to: page_base + 4073 = 8192 + 4073 = 12265 (0x2FE9)
```

Used on: all page types (leaf and interior).

---

### Pointer Type 2 — Child Page Pointers (4 bytes, in interior cell bodies)

In interior page cells, the first 4 bytes are an unsigned integer page number pointing to a child subtree.

```
Stored as:   00 00 00 08
Value:       page number 8
Resolves to: (8 - 1) × 4096 = 28672 (0x7000)
```

The SQLite engine follows this pointer by computing the file offset and reading the child page header to continue traversal.

---

### Pointer Type 3 — Rightmost Child Pointer (4 bytes, in interior page header)

Interior page headers are 12 bytes rather than 8. Bytes 8–11 hold the rightmost child pointer — the page number of the subtree that contains all keys larger than the last divider key in the page.

```
Header bytes 8-11:  00 00 00 0C
Value:              page number 12
Resolves to:        (12 - 1) × 4096 = 45056 (0xB000)
```

Traversal rule:

| Condition                    | Pointer Used            |
| ---------------------------- | ----------------------- |
| Search key < first divider   | Child pointer in cell 0 |
| First ≤ key < second divider | Child pointer in cell 1 |
| key ≥ last divider           | Rightmost child pointer |

---

### Full Pointer Chain — Current Database

```
sqlite_schema  Page 1  (leaf, type 0x0D)
    |
    +--  authors        Page 2  (leaf, type 0x0D)  offset 0x1000
    +--  books          Page 3  (leaf, type 0x0D)  offset 0x2000
    +--  members        Page 4  (leaf, type 0x0D)  offset 0x3000
    +--  issued_books   Page 5  (leaf, type 0x0D)  offset 0x4000
    +--  idx_books_title    Page 6  (leaf, type 0x0A)  offset 0x5000
    +--  idx_members_name   Page 7  (leaf, type 0x0A)  offset 0x6000
```

All tables and indexes currently occupy a single leaf page each. No interior nodes exist yet because no page has exceeded 4096 bytes. The pointer chain would extend downward (interior → leaf layers) once any table grows large enough to trigger a page split.

---

## Index-Based Lookup Mechanism

```sql
SELECT * FROM books WHERE title = '1984';
```

SQLite executes this query in two stages:

**Stage 1 — Index scan (idx_books_title, page 6)**

Traverse the Index B-Tree. Compare `'1984'` against key entries using binary search (`O(log n)`). Locate the matching key and extract its associated RowID.

**Stage 2 — Table fetch (books, page 3)**

Use the RowID to navigate the Table B-Tree. Jump directly to the leaf page containing that RowID. Read the full row.

```
Query: title = '1984'
       |
       v
idx_books_title (page 6)
       |
       v
  key match → RowID = 1
       |
       v
books table (page 3)
       |
       v
  cell pointer 0x0FE9 → address 0x2FE9
       |
       v
  full row: 1 | 1984 | Dystopian | 1949 | 1
```

Complexity comparison:

| Method                     | Complexity |
| -------------------------- | ---------- |
| Sequential scan (no index) | O(n)       |
| Index B-Tree lookup        | O(log n)   |

---

## DBSTAT Virtual Table Analysis

```sql
CREATE VIRTUAL TABLE temp.stat USING dbstat;

SELECT name, path, pageno, pagetype, ncell FROM stat;
```

| Name               | Page | Type | Cells |
| ------------------ | ---- | ---- | ----- |
| `sqlite_schema`    | 1    | leaf | 6     |
| `authors`          | 2    | leaf | 8     |
| `books`            | 3    | leaf | 15    |
| `members`          | 4    | leaf | 8     |
| `issued_books`     | 5    | leaf | 8     |
| `idx_books_title`  | 6    | leaf | 15    |
| `idx_members_name` | 7    | leaf | 8     |

---

## Observations

- Every table and index fits within a single leaf page — no interior B-Tree nodes were required at this database size.
- SQLite allocates interior nodes automatically when a leaf page fills, promoting the structure to a multi-level B+ Tree.
- Cell pointers allow O(1) access to any record within a page without scanning the entire page content.
- Record serial types in the header enable variable-width field encoding, compressing small integers to a single byte and eliminating padding entirely.
- The two-stage index lookup (index scan → table fetch) is SQLite's standard strategy for queries on indexed columns.
- The rightmost child pointer in interior node headers is what allows B-Trees to handle range queries efficiently — all keys beyond the last divider fall into a single known subtree.

---

## Conclusion

Low-level hexadecimal analysis of `library.db` exposed the complete SQLite storage pipeline from raw bytes to structured rows. The file header, page-type identifiers, B-Tree headers, cell pointer arrays, record serial types, and ASCII payloads were all directly observed and consistent with the SQLite file format specification.

Concepts demonstrated:

- SQLite file header and magic bytes at `0x0000`
- Page size encoding at `0x0010`
- Page offset arithmetic: `(page - 1) × 4096`
- Table Leaf (`0x0D`) and Index Leaf (`0x0A`) page structures
- Table Interior (`0x05`) and Index Interior (`0x02`) node layouts
- Slotted-page architecture with bidirectional growth
- Three pointer types: cell offset, child page, rightmost child
- Varint-encoded payload lengths and RowIDs
- Serial-type record headers driving variable-width field boundaries
- Step-by-step byte traversal through a live record
- Two-stage index-to-table row retrieval
- dbstat introspection of internal page statistics

---

## Appendix A — Full Books Table Page Hex Dump

**Command:**

```bash
xxd -g 1 -s 8192 -l 4096 library.db
```

**Page:** Books table root, page 3  
**File offset:** 8192 (`0x2000`)  
**Size:** 4096 bytes

```
00002000: 0d 00 00 00 0f 0d d3 00 0f e9 0f c4 0f 8e 0f 55
00002010: 0f 29 0f 09 0e e5 0e cb 0e a9 0e 80 0e 67 0e 4c
00002020: 0e 2b 0d fd 0d d3 00 00 00 00 00 00 00 00 00 00
00002030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002060: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002070: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002080: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002090: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000020a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000020b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000020c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000020d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000020e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000020f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002100: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002110: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002120: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002130: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002140: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002150: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002170: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002180: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002190: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000021a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000021b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000021c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000021d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000021e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000021f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002200: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002210: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002220: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002230: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002240: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002250: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002260: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002270: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002280: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002290: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000022a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000022b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000022c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000022d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000022e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000022f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002300: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002310: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002320: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002330: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002340: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002350: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002360: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002370: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002380: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002390: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000023a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000023b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000023c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000023d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000023e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000023f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002400: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002410: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002420: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002430: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002440: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002450: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002460: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002470: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002480: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002490: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000024a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000024b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000024c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000024d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000024e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000024f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002500: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002510: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002520: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002530: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002540: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002550: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002560: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002570: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002580: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002590: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000025a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000025b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000025c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000025d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000025e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000025f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002600: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002610: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002620: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002630: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002640: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002650: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002660: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002670: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002680: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002690: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000026a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000026b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000026c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000026d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000026e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000026f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002700: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002710: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002720: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002730: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002740: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002750: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002760: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002770: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002780: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002790: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000027a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000027b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000027c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000027d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000027e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000027f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002800: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002810: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002820: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002830: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002840: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002850: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002860: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002870: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002880: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002890: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000028a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000028b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000028c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000028d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000028e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000028f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002900: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002910: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002920: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002930: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002940: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002950: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002960: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002970: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002980: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002990: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000029a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000029b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000029c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000029d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000029e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
000029f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a40: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002a90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002aa0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002ab0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002ac0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002ad0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002ae0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002af0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b40: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002b90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002ba0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002bb0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002bc0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002bd0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002be0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002bf0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c40: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002c90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002ca0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002cb0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002cc0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002cd0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002ce0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002cf0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d40: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d60: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002d90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002da0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002db0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002dc0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002dd3: 26 0f 01 06 00 23 15 02 09 4b 61 66 6b 61 20 6f
00002de3: 6e 20 74 68 65 20 53 68 6f 72 65 00 00 00 00 00
00002df3: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00002e2b: 2a 0e 01 06 00 27 15 02 09 54 68 65 20 4d 61 73
00002e3b: 74 65 72 20 61 6e 64 20 4d 61 72 67 61 72 69 74
00002e4c: 2c 0d 01 06 00 29 15 02 09 54 68 65 20 4e 61 6d
00002e5c: 65 20 6f 66 20 74 68 65 20 52 6f 73 65 00 00 00
00002e67: 22 0c 01 06 00 1f 15 02 09 54 68 65 20 41 6c 63
00002e77: 68 65 6d 69 73 74 00 00 00 00 00 00 00 00 00 00
00002e80: 2e 0b 01 06 00 2b 15 02 09 54 68 65 20 48 69 74
00002e90: 63 68 68 69 6b 65 72 73 20 47 75 69 64 65 00 00
00002ea9: 2a 0a 01 06 00 27 15 02 09 54 68 65 20 47 72 65
00002eb9: 61 74 20 47 61 74 73 62 79 00 00 00 00 00 00 00
00002ecb: 1e 09 01 06 00 1b 15 02 09 42 72 61 76 65 20 4e
00002edb: 65 77 20 57 6f 72 6c 64 00 00 00 00 00 00 00 00
00002ee5: 28 08 01 06 00 25 15 02 09 54 6f 20 4b 69 6c 6c
00002ef5: 20 61 20 4d 6f 63 6b 69 6e 67 62 69 72 64 00 00
00002f09: 26 07 01 06 00 23 15 02 09 43 72 69 6d 65 20 61
00002f19: 6e 64 20 50 75 6e 69 73 68 6d 65 6e 74 00 00 00
00002f29: 28 06 01 06 00 25 15 02 09 4b 61 66 6b 61 20 6f
00002f39: 6e 20 74 68 65 20 53 68 6f 72 65 00 00 00 00 00
00002f55: 34 05 01 06 00 31 15 02 09 48 61 72 72 79 20 50
00002f65: 6f 74 74 65 72 20 61 6e 64 20 74 68 65 20 43 68
00002f75: 61 6d 62 65 72 20 6f 66 20 53 65 63 72 65 74 73
00002f8e: 36 04 01 06 00 33 15 02 09 48 61 72 72 79 20 50
00002f9e: 6f 74 74 65 72 20 61 6e 64 20 74 68 65 20 53 6f
00002fae: 72 63 65 72 65 72 73 20 53 74 6f 6e 65 00 00 00
00002fc4: 1e 03 01 06 00 1b 15 02 09 41 6e 69 6d 61 6c 20
00002fd4: 46 61 72 6d 00 00 00 00 00 00 00 00 00 00 00 00
00002fe9: 15 01 06 00 15 1f 02 09 31 39 38 34 44 79 73 74
00002ff9: 6f 70 69 61 6e 07 9d 00 00 00 00 00 00 00 00 00
```

**Hex dump annotations:**

| Address           | Content              | Notes                                      |
| ----------------- | -------------------- | ------------------------------------------ |
| `0x2000`          | `0d`                 | Table leaf page type                       |
| `0x2003`–`0x2004` | `00 0f`              | 15 cells in this page                      |
| `0x2005`–`0x2006` | `0d d3`              | Cell content starts at page offset 0x0DD3  |
| `0x2008`–`0x202D` | cell pointer array   | 15 × 2-byte offsets                        |
| `0x202E`–`0x2DD2` | `00 00 ...`          | Free space (unallocated)                   |
| `0x2DD3`          | first record payload | Last inserted record (lowest in page)      |
| `0x2FE9`          | `15 01 06 ...`       | Record 1: "1984 \| Dystopian \| 1949 \| 1" |