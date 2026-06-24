# SQLite3 Database Internal Structure Analysis using XXD

## Objective

The objective of this lab was to analyze the internal structure of a SQLite3 database file using hexadecimal dumps generated using the `xxd` utility. The experiment demonstrates how SQLite internally stores:

* Database headers
* B-tree pages
* Table records
* Metadata
* Cell pointer arrays
* Record payloads
* Schema definitions

The analysis was performed on a custom `clubs` database.

---

# Database Creation

The database was created using SQLite3.

## Schema

```sql
CREATE TABLE clubs (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    location TEXT NOT NULL
);
```

---

# Database Records

The table contains 5 club records.

Example rows:

| ID | Club Name     | Location      |
| -- | ------------- | ------------- |
| 1  | Chess Club    | New York      |
| 2  | Football Club | London        |
| 3  | Book Club     | San Francisco |
| 4  | Music Club    | Los Angeles   |
| 5  | Art Club      | Paris         |

---

# SQLite Database Metadata

The following commands were used:

```sql
PRAGMA page_size;
PRAGMA page_count;

SELECT name, rootpage
FROM sqlite_master;
```

## Output

```text
Page Size  : 4096 bytes
Page Count : 2
```

## Root Pages

| Object | Root Page |
| ------ | --------- |
| clubs  | 2         |

---

# Physical File Layout

Since each page is 4096 bytes:

| Page Number | File Offset |
| ----------- | ----------- |
| Page 1      | 0x0000      |
| Page 2      | 0x1000      |

---

# SQLite File Header Analysis

The beginning of the database file was inspected using:

```bash
xxd -g 1 -l 512 clubs
```

## Hex Dump

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

## ASCII Decoding

```text
SQLite format 3
```

This is the SQLite magic header that identifies the file as a valid SQLite3 database.

---

# SQLite Header Breakdown

| Bytes                | Meaning |
| -------------------- | ------- |
| 53 51 4C 69          | SQLi    |
| 74 65                | te      |
| 20 66 6F 72 6D 61 74 | format  |
| 20 33 00             | 3       |

---

# Page Size Analysis

Bytes:

```text
10 00
```

Hex:

```text
0x1000 = 4096
```

This matches the output of:

```sql
PRAGMA page_size;
```

---

# SQLite B-Tree Structure

SQLite internally stores all data using B-tree structures.

The database contains:

| Page | Type              | Purpose                |
| ---- | ----------------- | ---------------------- |
| 1    | Table B-tree      | sqlite_master metadata |
| 2    | Leaf Table B-tree | clubs table            |

---

# Overall Database Layout

```text
clubs
│
├── Page 1 (0x0000 – 0x0FFF)
│     ├── SQLite File Header
│     ├── sqlite_master B-tree
│     ├── Schema Records
│     └── CREATE TABLE statements
│
└── Page 2 (0x1000 – 0x1FFF)
      ├── clubs Table B-tree
      ├── Cell Pointer Array
      └── Club Records
```

---

# Analysis of Page 2 (Clubs Table)

The `clubs` table root page is page 2.

Command used:

```bash
xxd -g 1 -s 4096 -l 512 clubs
```

## Beginning of Page 2

```text
0d 00 00 00 05 0f 79 00
```

---

# Decoding the Page Header

SQLite leaf table page header format:

| Offset | Size    | Meaning               |
| ------ | ------- | --------------------- |
| 0      | 1 byte  | Page Type             |
| 1-2    | 2 bytes | First Freeblock       |
| 3-4    | 2 bytes | Number of Cells       |
| 5-6    | 2 bytes | Start of Cell Content |
| 7      | 1 byte  | Fragmented Free Bytes |

---

# Decoded Values

| Bytes | Value | Meaning                |
| ----- | ----- | ---------------------- |
| 0d    | 13    | Leaf Table B-tree Page |
| 00 00 | 0     | No freeblocks          |
| 00 05 | 5     | 5 records              |
| 0f 79 | 3961  | Cell content begins    |
| 00    | 0     | No fragmented bytes    |

---

# Cell Pointer Array

Immediately after the page header:

```text
0f dc
0f c3
0f a7
0f 8c
0f 79
```

These are 5 cell pointers.

Each pointer stores the offset of a record inside the page.

---

# Example Pointer Analysis

Pointer:

```text
0f dc
```

Hex:

```text
0x0FDC = 4060
```

Absolute file location:

```text
4096 + 4060 = 8156
```

Thus SQLite can directly locate the row payload using the pointer array.

---

# SQLite Page Organization

SQLite pages grow in opposite directions:

```text
+----------------------+
| B-tree Page Header   |
+----------------------+
| Cell Pointer Array   |
+----------------------+
| Free Space           |
|                      |
|                      |
+----------------------+
| Record Data          |
| (grows upward)       |
+----------------------+
```

This design minimizes memory movement during insertion and deletion.

---

# Record Payload Analysis

The record payload area begins at:

```text
0x0F79
```

Command used:

```bash
xxd -g 1 -s 8057 -l 512 clubs
```

---

# Extracted Record Data

The dump visibly contains real club data.

## Club Name

Hex:

```text
43 68 65 73 73 20 43 6c 75 62
```

ASCII:

```text
Chess Club
```

---

# Location Field

Hex:

```text
4e 65 77 20 59 6f 72 6b
```

ASCII:

```text
New York
```

---

# Additional Extracted Records

The dump also contains:

| Hex ASCII     | Decoded Text  |
| ------------- | ------------- |
| Football Club | London        |
| Book Club     | San Francisco |
| Music Club    | Los Angeles   |
| Art Club      | Paris         |

This proves that SQLite stores multiple rows compactly inside a single leaf B-tree page.

---

# SQLite Record Format

SQLite records are stored in the following format:

```text
[Payload Size]
[Row ID]
[Record Header Size]
[Serial Types]
[Actual Column Data]
```

---

# Analysis of sqlite_master

The `sqlite_master` table stores database metadata.

Hex dump contains:

```text
tableclubsclubs
```

ASCII decoding indicates metadata related to the `clubs` table.

---

# CREATE TABLE Statement Stored Internally

The output of:

```bash
strings clubs
```

contains:

```sql
CREATE TABLE clubs (id INTEGER PRIMARY KEY,name TEXT NOT NULL,location TEXT NOT NULL)
```

This proves that SQLite stores schema definitions directly inside the database file.

---

# Root Page References Stored Internally

The database internally stores references to:

| Object | Root Page |
| ------ | --------- |
| clubs  | 2         |

These match the output of:

```sql
SELECT name, rootpage FROM sqlite_master;
```

---

# Important Observations

1. SQLite stores everything using B-trees.
2. Metadata itself is stored as tables.
3. Records are variable-length.
4. Cell pointers provide fast lookup.
5. SQLite pages grow from opposite directions.
6. Multiple records can exist compactly within a single page.
7. SQL schema definitions are physically stored inside the database file.
8. The database file can be analyzed directly using hexadecimal tools like `xxd`.

