````markdown
# Exploring SQLite Internals using xxd

## Objective

This project explores the internal structure of a SQLite3 database using hexadecimal inspection.

The analysis includes:

- SQLite database header
- Page structure
- B-Tree pages
- Cell pointer arrays
- Record storage
- Address calculations

Database used:

```text
students.db
```

---

# Tools Used

- sqlite3
- xxd
- VS Code
- WSL

---

# Database Schema

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

---

# Records Inserted

| id | name | age |
|---|---|---|
| 1 | Alice | 21 |
| 2 | Bob | 22 |
| 3 | Charlie | 23 |
| 4 | David | 24 |

---

# Hex Dump Generation

```bash
xxd students.db > hexDump.txt
```

---

# SQLite Header

## Actual Hex Data

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300
00000010: 1000 0101 0040 2020 0000 0002 0000 0002
00000020: 0000 0000 0000 0000 0000 0001 0000 0004
00000030: 0000 0000 0000 0000 0000 0001 0000 0000
00000040: 0000 0000 0000 0000 0000 0000 0000 0000
00000050: 0000 0000 0000 0000 0000 0000 0000 0002
00000060: 002e 574a 0d00 0000 010f 8d00 0f8d 0000
```

---

# Header Interpretation

| Offset | Value | Meaning |
|---|---|---|
| 0x00 | SQLite format 3 | SQLite signature |
| 0x10 | 1000 | Page size |
| 0x1C | 00000002 | Total pages |

Page size:

```text
0x1000 = 4096 bytes
```

---

# Database Pages

| Page | Address Range |
|---|---|
| Page 1 | 0x0000 – 0x0FFF |
| Page 2 | 0x1000 – 0x1FFF |

---

# Root Page

Query used:

```sql
SELECT name, rootpage FROM sqlite_master;
```

Output:

```text
students|2
```

This means the `students` table starts at page 2.

---

# Page 2 Inspection

Command:

```bash
xxd -s 4096 -l 256 students.db
```

Output:

```text
00001000: 0d00 0000 040f d000 0ff4 0fea 0fdc 0fd0
00001010: 0000 0000 0000 0000 0000 0000 0000 0000
00001020: 0000 0000 0000 0000 0000 0000 0000 0000
00001030: 0000 0000 0000 0000 0000 0000 0000 0000
```

---

# B-Tree Page Analysis

| Value | Meaning |
|---|---|
| 0D | Leaf table B-Tree page |
| 0004 | Number of cells |
| 0FD0 | Start of cell content |

Cell pointers:

```text
0ff4 0fea 0fdc 0fd0
```

| Cell | Offset |
|---|---|
| 1 | 0x0FF4 |
| 2 | 0x0FEA |
| 3 | 0x0FDC |
| 4 | 0x0FD0 |

---

# Address Navigation

Example:

```text
Page base:
0x1000

Cell pointer:
0x0FDC
```

Actual row location:

```text
0x1000 + 0x0FDC
= 0x1FDC
```

---

# Visible Record Data

Using:

```bash
strings students.db
```

Output:

```text
David
Charlie
Alice
```

This shows SQLite stores TEXT values directly inside record payloads.

---

# Lookup Process

Example query:

```sql
SELECT * FROM students WHERE id = 3;
```

SQLite internally:

1. Opens root page
2. Reads B-Tree header
3. Traverses cell pointers
4. Finds matching row
5. Decodes payload

---

# Internal Structure

```text
students.db
│
├── SQLite Header
├── Page 1 (sqlite_master)
└── Page 2 (students table)
     ├── Page Header
     ├── Cell Pointer Array
     └── Record Payloads
```

---

# Conclusion

This project explored the internal storage architecture of SQLite3 using real hexadecimal dumps.

The analysis demonstrated:

- SQLite file headers
- page organization
- B-Tree pages
- cell pointer navigation
- payload storage
- manual address calculations

This provides insight into how SQLite stores and retrieves records internally.
````
