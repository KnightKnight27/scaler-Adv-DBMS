# SQLite3 Database Internal Structure Analysis using XXD

## Aim

The purpose of this experiment is to study how SQLite internally stores data inside a database file by examining hexadecimal dumps using the `xxd` utility.

The analysis includes:

- SQLite file headers
- Database pages
- B-tree organization
- Cell pointer arrays
- Record payload storage
- Schema metadata
- Internal page structure

The experiment was performed on a custom SQLite database named `clubs`.

---

# Database Setup

The database was created using SQLite3 on Pop!_OS Linux.

## Table Schema

```sql
CREATE TABLE clubs (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    location TEXT NOT NULL
);
```

---

# Inserted Records

The `clubs` table contains 5 records.

| ID | Club Name | Location |
|----|------------|------------|
| 1 | Robotics Club | Bangalore |
| 2 | Photography Club | Mumbai |
| 3 | Gaming Club | Hyderabad |
| 4 | Coding Club | Pune |
| 5 | Music Society | Chennai |

---

# Database Information

The following SQLite commands were used to inspect database metadata:

```sql
PRAGMA page_size;
PRAGMA page_count;

SELECT name, rootpage
FROM sqlite_master;
```

## Output

```text
4096
2
clubs|2
```

This indicates:

| Parameter | Value |
|------------|-------|
| Page Size | 4096 bytes |
| Total Pages | 2 |
| Root Page of `clubs` | 2 |

---

# SQLite File Layout

Since each page size is 4096 bytes:

| Page | File Offset |
|------|-------------|
| Page 1 | `0x0000` |
| Page 2 | `0x1000` |

Database structure:

```text
clubs
│
├── Page 1
│   ├── SQLite File Header
│   ├── sqlite_master Table
│   └── Schema Definitions
│
└── Page 2
    ├── clubs Table B-tree
    ├── Cell Pointer Array
    └── Record Payloads
```

---

# SQLite File Header Analysis

The first 512 bytes of the database file were inspected using:

```bash
xxd -g 1 -l 512 clubs
```

## Important Header Bytes

```text
53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

ASCII decoding:

```text
SQLite format 3
```

This is the SQLite magic signature used to identify valid SQLite3 database files.

---

# Header Breakdown

| Hex Bytes | Meaning |
|------------|---------|
| `53 51 4C 69` | SQLi |
| `74 65` | te |
| `20 66 6F 72 6D 61 74` | format |
| `20 33 00` | 3 |

---

# Page Size Verification

Bytes found in the header:

```text
10 00
```

Hexadecimal conversion:

```text
0x1000 = 4096
```

This matches the output of:

```sql
PRAGMA page_size;
```

---

# SQLite B-Tree Organization

SQLite stores all information internally using B-tree structures.

The database contains two pages:

| Page | Type | Purpose |
|------|------|----------|
| 1 | Table B-tree | `sqlite_master` metadata |
| 2 | Leaf Table B-tree | `clubs` table records |

---

# Analysis of Page 2

The `clubs` table root page is page number 2.

The page was examined using:

```bash
xxd -g 1 -s 4096 -l 512 clubs
```

Beginning bytes:

```text
0d 00 00 00 05 0f 7f 00
```

---

# Decoding the Page Header

SQLite leaf table page header structure:

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 1 byte | Page Type |
| 1-2 | 2 bytes | First Freeblock |
| 3-4 | 2 bytes | Number of Cells |
| 5-6 | 2 bytes | Start of Cell Content |
| 7 | 1 byte | Fragmented Free Bytes |

Decoded values:

| Bytes | Value | Meaning |
|-------|-------|---------|
| `0d` | 13 | Leaf Table B-tree Page |
| `00 00` | 0 | No freeblocks |
| `00 05` | 5 | Total records |
| `0f 7f` | 3967 | Cell content area |
| `00` | 0 | No fragmented bytes |

---

# Cell Pointer Array

Immediately after the page header:

```text
0f e4
0f c8
0f ae
0f 99
0f 7f
```

Each value points to the location of a record inside the page.

Example:

```text
0f e4 = 4068
```

Absolute file location:

```text
4096 + 4068 = 8164
```

SQLite uses these pointers for efficient row access.

---

# Internal SQLite Page Structure

SQLite pages grow from opposite directions.

```text
+----------------------+
| B-tree Page Header   |
+----------------------+
| Cell Pointer Array   |
+----------------------+
| Free Space           |
|                      |
+----------------------+
| Record Payloads      |
| (grows upward)       |
+----------------------+
```

This structure minimizes data movement during insertions and deletions.

---

# Record Payload Analysis

The payload area was analyzed using:

```bash
xxd -g 1 -s 8057 -l 512 clubs
```

The dump clearly contains inserted records.

---

# Example Record Extraction

## Robotics Club

Hex:

```text
52 6f 62 6f 74 69 63 73 20 43 6c 75 62
```

ASCII:

```text
Robotics Club
```

---

## Bangalore

Hex:

```text
42 61 6e 67 61 6c 6f 72 65
```

ASCII:

```text
Bangalore
```

---

# Additional Extracted Records

The hexadecimal dump also contains:

| Stored Text |
|-------------|
| Photography Club |
| Gaming Club |
| Coding Club |
| Music Society |
| Mumbai |
| Hyderabad |
| Pune |
| Chennai |

This demonstrates that SQLite stores multiple rows compactly inside a single B-tree leaf page.

---

# SQLite Record Format

SQLite stores records internally in the following structure:

```text
[Payload Size]
[Row ID]
[Header Size]
[Serial Type Codes]
[Column Data]
```

The records are variable-length to improve storage efficiency.

---

# sqlite_master Analysis

The `sqlite_master` table stores metadata related to database objects.

Running:

```bash
strings clubs
```

revealed:

```sql
CREATE TABLE clubs (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    location TEXT NOT NULL
)
```

This confirms that SQLite stores schema definitions directly inside the database file.

---

# Important Observations

1. SQLite internally uses B-tree structures for storage.
2. Metadata is stored as regular tables.
3. Records are stored in variable-length format.
4. Cell pointers allow direct access to rows.
5. SQLite pages grow from opposite directions.
6. Multiple records are compactly packed into a single page.
7. SQL schema definitions are physically embedded inside the database file.
8. Hexadecimal utilities like `xxd` can directly expose SQLite internals.

---

# Commands Used

## Open Database

```bash
sqlite3 clubs
```

## Display First 512 Bytes

```bash
xxd -g 1 -l 512 clubs
```

## Dump Page 2

```bash
xxd -g 1 -s 4096 -l 512 clubs
```

## Extract Strings

```bash
strings clubs
```

## View Metadata

```sql
PRAGMA page_size;
PRAGMA page_count;
SELECT name, rootpage FROM sqlite_master;
```

---

# Conclusion

This experiment successfully demonstrated how SQLite organizes and stores information internally using:

- B-tree pages
- Cell pointer arrays
- Compact variable-length records
- Embedded schema metadata

Using `xxd`, the internal physical structure of the SQLite database could be directly inspected and correlated with SQLite’s logical organization.