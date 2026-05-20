# SQLite3 Internal Storage Analysis
### Hexadecimal Examination of B-Tree Page Architecture

Name: Vimal Kumar Yadav
RollNo: 24BCS10273
---

**Platform:** Fedora KDE Plasma Desktop  
**Tools:** SQLite3 · xxd · dbstat Virtual Table  
**Scope:** Page layout, B-Tree nodes, cell pointers, record encoding, index navigation

---

## Objective

Explore the internal storage architecture of SQLite3 databases through low-level hexadecimal analysis. The investigation covers SQLite page structure, B-Tree node layout, cell pointer arrays, record encoding, and the index-based lookup mechanism — observed directly from raw binary file dumps.

---

## Tools

| Tool | Purpose |
|------|---------|
| `sqlite3` | Database creation and querying |
| `xxd` | Hex dump analysis |
| `dbstat` | SQLite internal page statistics |

---

## Database Design

A Library Management database was constructed to generate realistic B-Tree structures across multiple tables and indexes.

### Schema

```sql
CREATE TABLE authors (
    author_id INTEGER PRIMARY KEY,
    name      TEXT,
    country   TEXT
);

CREATE TABLE books (
    book_id        INTEGER PRIMARY KEY,
    title          TEXT,
    genre          TEXT,
    published_year INTEGER,
    author_id      INTEGER,
    FOREIGN KEY(author_id) REFERENCES authors(author_id)
);

CREATE TABLE members (
    member_id  INTEGER PRIMARY KEY,
    name       TEXT,
    department TEXT
);

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

| Name | Type | Root Page |
|------|------|-----------|
| `authors` | table | 2 |
| `books` | table | 3 |
| `members` | table | 4 |
| `issued_books` | table | 5 |
| `idx_books_title` | index | 6 |
| `idx_members_name` | index | 7 |

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

The database file contains **7 pages** in total.

---

## Page Address Calculation

SQLite pages are stored sequentially. The byte offset for any page is calculated as:

```
offset = (page_number - 1) × page_size
```

**Example — Books table (root page 3):**

```
offset = (3 - 1) × 4096 = 8192
```

The Books table B-Tree begins at byte offset **8192** in the file.

---

## File Header Analysis

```bash
xxd library.db | head -n 40
```

### SQLite Signature

```
53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

ASCII interpretation:

```
SQLite format 3
```

Confirms a valid SQLite3 database file.

### Page Size Field

Bytes at offset `0x10`:

```
10 00
```

Hex value `0x1000 = 4096` — consistent with `PRAGMA page_size`.

---

## B-Tree Page Analysis

### Inspection Command

```bash
xxd -g 1 -s 8192 -l 256 library.db
```

### Raw Header — Books Table

```
0d 00 00 00 0f 0d d3 00
```

### B-Tree Header Interpretation

| Bytes | Field | Value |
|-------|-------|-------|
| `0d` | Page type | Table Leaf Page |
| `00 00` | First freeblock offset | None |
| `00 0f` | Number of cells | 15 |
| `0d d3` | Start of cell content area | offset 0x0DD3 |
| `00` | Fragmented free bytes | 0 |

The byte `0x0D` identifies a **Table Leaf B-Tree Page** — a page that stores actual table rows rather than internal navigation nodes.

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

### Cell Pointer Array

Immediately following the B-Tree header, SQLite stores 2-byte offsets pointing to each record:

```
0f e9
0f c4
0f 8e
```

Each value is a byte offset from the start of the page to the corresponding record payload.

---

## Record Payload Analysis

### Jump to First Record

```bash
xxd -g 1 -s $((8192 + 4073)) -l 80 library.db
```

### Raw Bytes

```
15 01 06 00 15 1f 02 09
31 39 38 34
44 79 73 74 6f 70 69 61 6e
07 9d
```

### Record Decoding

| Bytes | Field | Interpretation |
|-------|-------|----------------|
| `15` | Payload length | 21 bytes |
| `01` | RowID | 1 |
| `06` | Header size | 6 bytes |
| `15` | Serial type | TEXT, length 4 |
| `1f` | Serial type | TEXT, length 9 |
| `02` | Serial type | 2-byte integer |
| `09` | Serial type | Integer constant 1 |

### ASCII Decoding

**Title field:**

```
31 39 38 34  →  1984
```

**Genre field:**

```
44 79 73 74 6f 70 69 61 6e  →  Dystopian
```

### Reconstructed Row

```
book_id | title | genre     | published_year | author_id
1       | 1984  | Dystopian | 1949           | 1
```

### Table Leaf Cell Structure

Every record in a Table Leaf page follows this layout:

```
[ payload length ]  varint
[ row id         ]  varint
[ record header  ]  serial type descriptors
[ record body    ]  actual field values
```

---

## Index B-Tree Analysis

### Inspection Command

```bash
xxd -g 1 -s $(( (6 - 1) * 4096 )) -l 128 library.db
```

This targets page 6 — the `idx_books_title` index.

### Raw Header

```
0a 00 00 00 0f 0e a8 00
```

### Index Page Header Interpretation

| Bytes | Field | Value |
|-------|-------|-------|
| `0a` | Page type | Index Leaf Page |
| `00 0f` | Number of cells | 15 |
| `0e a8` | Start of index payload area | offset 0x0EA8 |

The byte `0x0A` identifies an **Index Leaf B-Tree Page**.

---

## Table B-Tree vs. Index B-Tree

| Property | Table B-Tree | Index B-Tree |
|----------|-------------|--------------|
| Page type byte | `0x0D` | `0x0A` |
| Content | Full row records | Indexed key + RowID reference |
| Purpose | Primary data storage | Accelerated key lookup |

---

## Index-Based Lookup Mechanism

```sql
SELECT * FROM books WHERE title = '1984';
```

SQLite executes this in two stages:

1. **Index scan** — Traverse `idx_books_title` (Index B-Tree) to locate the key `'1984'` and retrieve its associated RowID.
2. **Table lookup** — Use the RowID to jump directly to the corresponding row in the Books Table B-Tree.

This reduces lookup complexity from linear to logarithmic:

```
Sequential scan:  O(n)
Index lookup:     O(log n)
```

---

## DBSTAT Virtual Table Analysis

```sql
CREATE VIRTUAL TABLE temp.stat USING dbstat;

SELECT name, path, pageno, pagetype, ncell
FROM stat;
```

### Output

| Name | Page | Type | Cells |
|------|------|------|-------|
| `sqlite_schema` | 1 | leaf | 6 |
| `authors` | 2 | leaf | 8 |
| `books` | 3 | leaf | 15 |
| `members` | 4 | leaf | 8 |
| `issued_books` | 5 | leaf | 8 |
| `idx_books_title` | 6 | leaf | 15 |
| `idx_members_name` | 7 | leaf | 8 |

---

## Observations

- Every table and index fits within a single leaf page — no interior B-Tree nodes were required at this database size.
- SQLite allocates additional interior nodes automatically as data volume grows, promoting the structure to a multi-level B+ Tree.
- Cell pointers allow O(1) access to any record within a page without scanning the entire page content.
- Record serial types in the header enable variable-width encoding, compressing small integers to a single byte.
- The two-pass lookup (index → table) is the standard SQLite strategy for covered index queries.

---

## Conclusion

Low-level hex analysis of `library.db` revealed the complete storage pipeline from raw bytes to structured rows. The file header, page-type identifiers, B-Tree headers, cell pointer arrays, record serial types, and ASCII payloads were all directly observable and consistent with SQLite's published file format specification.

The core concepts demonstrated:

- SQLite file header and magic bytes
- Page size and offset arithmetic
- Table Leaf (`0x0D`) and Index Leaf (`0x0A`) page types
- Slotted-page architecture with bidirectional growth
- Varint-encoded payload lengths and RowIDs
- Serial-type record headers for variable-width field encoding
- Two-stage index-to-table row retrieval
- dbstat introspection of internal page statistics

---
>>>>>>> c8427bb (Added analysis of indexing and trees in sqlite3)
