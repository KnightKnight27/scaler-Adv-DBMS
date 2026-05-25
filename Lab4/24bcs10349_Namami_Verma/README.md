# SQLite3 Internal Structure Analysis

## Student Information

- Name: Namami Verma
- Roll Number: 24BCS10349
- Lab: SQLite3 Internal Structure and Hex Dump Analysis

---

# Objective

The objective of this lab is to study the internal structure of a SQLite3 database file using hexadecimal analysis. The lab demonstrates how SQLite stores records internally using B-Tree pages, cell pointer arrays, and page-based storage.

Tools used:

- sqlite3
- xxd
- strings
- Linux terminal

---

# Database Creation

## Command Used

```bash
sqlite3 lab.db
```

## Table Creation

```sql
CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

## Inserted Records

```sql
INSERT INTO students(name, age) VALUES
('Alice',21),
('Bob',22),
('Charlie',23),
('David',24),
('Eve',25),
('Frank',26),
('Grace',27),
('Helen',28);
```

---

# Verification of Data

## Command Used

```sql
SELECT * FROM students;
```

## Output

```text
1|Alice|21
2|Bob|22
3|Charlie|23
4|David|24
5|Eve|25
6|Frank|26
7|Grace|27
8|Helen|28
```

---

# SQLite Database File Information

## File Created

```text
lab.db
```

## File Size

```text
8 KB
```

---

# SQLite Page Information

## Page Size

### Command Used

```bash
sqlite3 lab.db "PRAGMA page_size;"
```

### Output

```text
4096
```

### Explanation

SQLite organizes the database into fixed-size pages.

Each page in this database is:

```text
4096 bytes
```

---

# Total Number of Pages

## Command Used

```bash
sqlite3 lab.db "PRAGMA page_count;"
```

## Output

```text
2
```

## Explanation

The database contains 2 pages.

| Page Number | Address Range | Purpose |
|---|---|---|
| Page 1 | 0x0000 – 0x0FFF | SQLite header and schema |
| Page 2 | 0x1000 – 0x1FFF | students table records |

---

# Generating Hex Dump

## Command Used

```bash
xxd -g 1 lab.db > dump.txt
```

This command generates a hexadecimal dump of the SQLite database file.

---

# SQLite Header Analysis

## Command Used

```bash
xxd -l 64 lab.db
```

## Output

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0002 0000 0002
00000020: 0000 0000 0000 0000 0000 0001 0000 0004
00000030: 0000 0000 0000 0000 0000 0001 0000 0000
```

---

# SQLite Signature Analysis

The first 16 bytes contain the SQLite database signature.

| Hex Bytes | ASCII | Meaning |
|---|---|---|
| 53 51 4c 69 | SQLi | SQLite identifier |
| 74 65 20 66 | te f | Part of signature |
| 6f 72 6d 61 | orma | Part of signature |
| 74 20 33 00 | t 3 | SQLite format version |

The complete signature is:

```text
SQLite format 3
```

This confirms that the file is a valid SQLite3 database.

---

# SQLite Page Size Analysis

At offset 16:

```text
10 00
```

Hexadecimal value:

```text
0x1000
```

Decimal equivalent:

```text
4096
```

This confirms the SQLite page size is 4096 bytes.

---

# B-Tree Structure in SQLite

SQLite stores tables internally using B-Tree structures.

B-Trees provide:

- Fast searching
- Efficient insertion
- Efficient deletion
- Ordered storage of records

SQLite pages are categorized into:

| Page Type | Hex Value | Meaning |
|---|---|---|
| 0x0D | Leaf Table Page | Stores actual rows |
| 0x05 | Interior Table Page | Stores child pointers |
| 0x0A | Leaf Index Page | Stores index values |
| 0x02 | Interior Index Page | Stores index navigation |

---

# Real B-Tree Page Analysis

## Command Used

```bash
xxd -s 4096 -l 128 -g 1 lab.db
```

## Output

```text
00001000: 0d 00 00 00 08 0f a2 00 0f f4 0f ea 0f dc 0f d0
00001010: 0f c6 0f ba 0f ae 0f a2 00 00 00 00 00 00 00 00
00001020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00001030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

---

# B-Tree Header Analysis

The beginning of the page contains the B-Tree page header.

| Offset | Bytes | Value | Meaning |
|---|---|---|---|
| 0 | 1 | 0d | Leaf table B-tree page |
| 1–2 | 2 | 0000 | No freeblock |
| 3–4 | 2 | 0008 | Number of cells = 8 |
| 5–6 | 2 | 0fa2 | Start of cell content area |
| 7 | 1 | 00 | Fragmented free bytes |

---

# Explanation of B-Tree Header

## Page Type

```text
0d
```

This indicates the page is a:

```text
Leaf Table B-Tree Page
```

This page stores actual table row data.

---

## Number of Cells

```text
0008
```

This means the page contains:

```text
8 cells (8 rows)
```

This matches the 8 inserted student records.

---

## Cell Content Area

```text
0fa2
```

This indicates that the cell content area begins at offset:

```text
0x0FA2
```

inside the page.

SQLite stores records from the bottom of the page upward.

---

# Cell Pointer Array Analysis

The cell pointer array begins immediately after the page header.

## Cell Pointer Values

```text
0f f4
0f ea
0f dc
0f d0
0f c6
0f ba
0f ae
0f a2
```

Each pointer references the location of a row record inside the page.

---

# Cell Pointer Address Calculation

Page 2 starts at:

```text
0x1000
```

Formula used:

```text
Real Address = Page Start + Cell Offset
```

---

# Real Cell Addresses

| Cell Pointer | Decimal Offset | Real Address |
|---|---|---|
| 0ff4 | 4084 | 0x1ff4 |
| 0fea | 4074 | 0x1fea |
| 0fdc | 4060 | 0x1fdc |
| 0fd0 | 4048 | 0x1fd0 |
| 0fc6 | 4038 | 0x1fc6 |
| 0fba | 4026 | 0x1fba |
| 0fae | 4014 | 0x1fae |
| 0fa2 | 4002 | 0x1fa2 |

These addresses represent the actual physical locations of records inside the SQLite database file.

---

# Record Storage in SQLite

Each SQLite record contains:

| Component | Description |
|---|---|
| Payload Size | Size of record |
| Row ID | Primary key value |
| Record Header | Metadata |
| Serial Types | Data type information |
| Payload Data | Actual row values |

The payload contains values such as:

```text
Alice
Bob
Charlie
```

---

# Extracting Stored Strings

## Command Used

```bash
strings lab.db
```

## Output

```text
SQLite format 3
3tablestudentsstudents
CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
Helen
Grace
Frank
David
Charlie
Alice
```

---

# Explanation of String Output

The output confirms that SQLite stores:

- Table schema
- SQL statements
- Actual row payload data

inside the database pages.

The names appear directly in the database file because SQLite stores text payloads inside B-Tree leaf pages.

---

# SQLite Lookup Process

Example query:

```sql
SELECT * FROM students WHERE id = 5;
```

SQLite performs the following steps internally:

1. Start at the root B-Tree page
2. Read the page header
3. Read the cell pointer array
4. Compare row IDs
5. Navigate to the matching cell
6. Read the payload data

---

# Navigation Through B-Tree Nodes

SQLite uses B-Tree traversal for efficient searching.

The process involves:

- Reading node headers
- Following cell pointers
- Comparing row IDs
- Accessing record payloads

This structure allows fast lookup operations with logarithmic complexity.

---

# Important Observations

1. SQLite databases are page-based.
2. Each page has a fixed size.
3. Tables are stored as B-Trees.
4. Leaf pages store actual row data.
5. Cell pointers reference row locations.
6. Records are stored from bottom to top inside pages.
7. SQLite stores text payload directly in pages.

---

# Files Generated

| File Name | Description |
|---|---|
| lab.db | SQLite database file |
| dump.txt | Hex dump of database |
| README.md | Internal structure analysis |

---

# Conclusion

This lab demonstrated the internal working of SQLite3 database storage using hexadecimal analysis.

The experiment showed:

- SQLite page structure
- SQLite database headers
- B-Tree organization
- Cell pointer arrays
- Record storage
- Physical addresses of rows
- Lookup navigation inside B-Tree pages

The analysis confirmed that SQLite uses page-oriented B-Tree structures to efficiently organize and retrieve records from the database.