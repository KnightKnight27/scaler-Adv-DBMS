# SQLite Database Internal Structure Inspection using `xxd` (System Design Assignment)
BY PATEL JASH (24BCS10632) — Batch B
## Aim

To examine the low-level binary layout of an SQLite database file with the help of hex dump utilities, and to study how SQLite organizes schema metadata, page allocation, B-Tree hierarchies, and individual record storage at the byte level.

---

## Tools Used

- SQLite3 CLI
- `xxd`
- Linux Terminal
- `students.db`

---

# Step 1 — Creating / Opening Database

Command used:

```bash
sqlite3 students.db
```

Listing tables:

```sql
.tables
```

Output:

```txt
students
```

---

# Step 2 — Dumping Database in Hexadecimal Form

Command used:

```bash
xxd -g 1 students.db > dump.txt
```

### Meaning of command

| Command Part | Description |
|---|---|
| `xxd` | Generates hexadecimal dump |
| `-g 1` | Groups bytes individually |
| `students.db` | SQLite database file |
| `>` | Redirects output |
| `dump.txt` | Stores dump into text file |

---

# Step 3 — Inspecting SQLite Header

Beginning of dump:

```txt
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

ASCII interpretation:

```txt
SQLite format 3
```

The presence of this magic string verifies that the file is indeed a legitimate SQLite version 3 database.

---

# Step 4 — Determining Page Size

At offset `0x10`:

```txt
00000010: 10 00
```

Hexadecimal value:

```txt
0x1000 = 4096
```

Hence,

```txt
Page Size = 4096 bytes
```

SQLite partitions the database file into uniform-sized pages, each acting as the fundamental unit of I/O.

---

# Step 5 — Finding Total Number of Pages

At offset `0x1C`:

```txt
00 00 00 04
```

This means:

```txt
Total Pages = 4
```

Total database size:

```txt
4 × 4096 = 16384 bytes
```

---

# Step 6 — Beginning of First B-Tree Page

The SQLite file header spans the initial 100 bytes of page 1.

Immediately following offset `0x64`, the B-Tree page content starts.

Observed bytes:

```txt
0d 0f f8 00 03
```

---

# Step 7 — Decoding B-Tree Page Header

## Page Type

First byte:

```txt
0d
```

Meaning:

```txt
Table Leaf Page
```

SQLite page type values:

| Hex | Meaning |
|---|---|
| `0D` | Table Leaf |
| `05` | Table Interior |
| `0A` | Index Leaf |
| `02` | Index Interior |

---

## Number of Cells

Bytes:

```txt
00 03
```

Meaning:

```txt
3 cells present in page
```

Each cell corresponds to a single row or record housed within this B-Tree leaf page.

---

# Step 8 — Cell Pointer Array

Observed values:

```txt
0e 77
0e 77
0f c7
```

These offsets point to the byte positions where the actual cell payloads reside within the page.

The general page layout in SQLite follows this pattern:

- Page header occupies the topmost bytes
- Cell pointer array sits right after the header
- Record data is packed towards the bottom of the page

---

# Step 9 — Free Space Observation

Large regions of:

```txt
00 00 00 00
```

were observed.

These null bytes indicate unallocated regions within the page.

SQLite reserves the entire page regardless of how little data is actually stored, leaving room for future row insertions without requiring immediate page splits.

---

# Step 10 — Schema Information Stored Internally

At offset around `0xE70`, readable text appeared:

```txt
table
students
CREATE TABLE students
```

This reveals that SQLite persists schema definitions as regular data records within a special system catalog table known as:

```sql
sqlite_schema
```

---

# Step 11 — Observed CREATE TABLE Statement

The following SQL statement was visible directly in hexadecimal dump:

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)
```

This demonstrates that the full SQL DDL statements are embedded verbatim within the database file itself, making the schema self-describing.

---

# Step 12 — Root Page Information

Inside schema record:

```txt
students 02
```

Meaning:

```txt
Root page of students table = Page 2
```

---

# Step 13 — Inspecting Page 2

At offset:

```txt
00001000
```

which equals:

```txt
4096 decimal
```

New B-Tree page begins.

Observed bytes:

```txt
0d 00 00 00 02
```

Interpretation:

| Value | Meaning |
|---|---|
| `0d` | Table leaf page |
| `0002` | 2 cells |

This corresponds to the 2 inserted student records.

---

# Overall Database Structure

Current database organization:

```txt
Page 1  -> sqlite_schema table
Page 2  -> students table data
Page 3  -> auto-generated index
Page 4  -> additional index/internal structure
```

---

# Observations

1. The entire database is divided into pages of a fixed size (4096 bytes in this case), which serve as the basic unit of storage and retrieval.
2. Every user table is backed by a B-Tree data structure, with leaf pages holding the actual row data.
3. Schema metadata — including table names and their DDL — is kept as ordinary records inside the `sqlite_schema` system table on page 1.
4. The raw SQL `CREATE TABLE` statements are stored byte-for-byte within the database file, allowing SQLite to reconstruct the schema on every connection.
5. Page headers and cell pointer arrays together form a directory mechanism that lets SQLite locate individual records efficiently within a page.
6. Unused portions of each page are filled with zero bytes and remain available for accommodating new rows without immediately allocating additional pages.

---

# Conclusion

By examining the raw hex dump of the SQLite database file, the internal storage mechanisms were thoroughly explored.

Key takeaways from the experiment:

- The 100-byte file header carries essential metadata such as the magic string, page size, and page count
- Data is organized into discrete, fixed-size pages that form a predictable on-disk layout
- B-Tree leaf and interior pages provide the structural backbone for efficient record lookup
- Cell pointer arrays act as an in-page index, mapping offsets to individual records
- Schema DDL is persisted within the database itself, making each `.db` file fully self-contained
- Free space management within pages allows for growth without constant file restructuring

This hands-on inspection provided practical insight into how a lightweight relational database engine like SQLite physically arranges and manages data at the binary level, going beyond the abstraction provided by SQL queries.

---

## Commands Used During Experiment

```bash
sqlite3 students.db
.tables
xxd -g 1 students.db > dump.txt
less dump.txt
```

---

## Reference Dump Snippets

```txt
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
00000010: 10 00
00000064: 0d 0f f8 00 03
00000e70: table students CREATE TABLE students
00001000: 0d 00 00 00 02
```

