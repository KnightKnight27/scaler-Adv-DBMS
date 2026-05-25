# Lab 4 — Inspecting a SQLite Database Using Hex Dump

## Overview

The objective of this lab was to understand how SQLite stores data internally inside a database file. Instead of only interacting with the database using SQL queries, this lab focused on analyzing the raw binary structure of the `.db` file using a hexadecimal dump.

In this experiment, a SQLite database was created, records were inserted into a table, and the resulting database file was examined using a hex dump utility. This helped in understanding SQLite pages, B-tree structures, record storage, and page layout at the byte level.

---

# Step 1 — Creating the Database

A SQLite database named `lab4.db` was created using the SQLite command-line tool.

The following SQL command was used to create a table:

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    grade TEXT
);
```

After creating the table, sample records were inserted:

```sql
INSERT INTO students (name, grade) VALUES
('Rahul', 'A'),
('Sneha', 'B'),
('Kiran', 'A+'),
('Meera', 'B+');
```

These rows were inserted to generate actual table records inside the SQLite database file.

---

# Step 2 — Generating the Hex Dump

After exiting SQLite, a hexadecimal dump of the database file was generated.

Command used:

```bash
xxd lab4.db > hexdump.txt
```

This command converts the binary database file into a readable hexadecimal representation and stores the output inside `hexdump.txt`.

---

# Step 3 — Understanding SQLite File Structure

SQLite databases are divided into fixed-size pages. Each page stores part of the database structure such as schemas, indexes, or table records.

## Important Observations

| Item | Description |
|------|-------------|
| File Type | SQLite Database |
| Default Page Size | 4096 bytes |
| Page 1 | SQLite schema and metadata |
| Page 2 | Table data (`students` table) |

The first page contains database metadata and the `sqlite_master` table. The actual row data for the `students` table is stored in the root B-tree page.

---

# Step 4 — Inspecting the SQLite Header

The beginning of the hex dump contains the SQLite file signature.

Example:

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300
```

This translates to:

```text
SQLite format 3
```

This confirms that the file is a valid SQLite version 3 database.

The SQLite header also stores important metadata such as:

- Page size
- File format version
- Number of pages
- Schema information

---

# Step 5 — Inspecting the B-tree Page

The second page of the database contains the actual records inserted into the `students` table.

The page begins near offset:

```text
00001000
```

Example page header:

```text
00001000: 0d00 0000 040f c200
```

## Explanation of Important Bytes

| Hex Value | Meaning |
|-----------|---------|
| `0d` | Table leaf B-tree page |
| `0004` | Number of cells/rows |
| `0fc2` | Start of cell content area |

SQLite stores records inside cells within B-tree pages. The page header stores metadata about the page and pointers to the records.

---

# Step 6 — Understanding Cell Pointers

After the page header, SQLite stores a cell pointer array.

Example:

```text
0fe8 0fdd 0fce 0fc2
```

Each value represents the location of a record inside the page.

These pointers help SQLite quickly locate rows without scanning the entire page.

---

# Step 7 — Inspecting Record Storage

At the end of the page, the actual row data can be observed.

Example:

```text
RahulA
SnehaB
KiranA+
MeeraB+
```

The records are stored in SQLite record format, which contains:

- Payload length
- Row ID
- Column type information
- Actual column data

SQLite stores text values in ASCII/UTF-8 format inside the page.

---

# Observations

During this experiment, the following observations were made:

- SQLite databases are page-based.
- SQLite uses B-tree structures for table storage.
- Table rows are stored as cells inside pages.
- Cell pointers are used for efficient row access.
- SQLite stores metadata and records in binary format.
- Hex dumps help visualize low-level database storage.

---

# What I Learned

This lab helped in understanding:

- Internal structure of SQLite databases
- SQLite page organization
- B-tree page layout
- Record storage format
- Use of hexadecimal dumps for low-level analysis
- Relationship between SQL data and binary storage

The experiment provided practical exposure to how databases manage data internally beyond standard SQL operations.

---

# Files Included

| File | Description |
|------|-------------|
| `lab4.db` | SQLite database file |
| `hexdump.txt` | Hexadecimal dump of the database |
| `README.md` | Detailed lab explanation |

---

# Conclusion

This lab successfully demonstrated how SQLite stores data internally using pages and B-tree structures. By generating and analyzing a hexadecimal dump of the database file, it was possible to observe how records, headers, and pointers are physically stored inside the database.

The experiment improved understanding of database internals and low-level file organization used by SQLite.