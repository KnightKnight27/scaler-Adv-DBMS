# Lab 4 - SQLite B-Tree Structure Inspection

Name : Mayank Soni
Roll No : 24BCS10127

For this assignment, I created a SQLite database, populated it with a table and multiple rows, and used the `xxd` hex dump utility to inspect the internal B-tree structure where the table data is actually stored.

## Methodology

1. Created a database file named `lab4.db`.
2. Created a table for student records:
   ```sql
   CREATE TABLE students (id INTEGER PRIMARY KEY, name TEXT);
   ```
3. Inserted 5 records into the table:
   ```sql
   INSERT INTO students(name) VALUES ('Alice'), ('Bob'), ('Charlie'), ('David'), ('Eve');
   ```
4. Dumped the database file to a hex format using `xxd lab4.db > hexdump.txt`.
5. Inspected the database metadata using SQLite Pragmas and Schema queries:
   - Page size is `4096` bytes.
   - Total page count is `2`.
   - The root page for the `students` table is page `2`.

## B-Tree Hex Analysis

Since the page size is 4096 (which is `0x1000` in hex), page 1 is at offset `0x0000`, and page 2 (the root page for our table) starts exactly at offset `0x1000`. Here is the hex dump of the beginning of page 2:

```text
00001000: 0d00 0000 050f d000 0ff6 0fee 0fe2 0fd8
```

Breaking down the page header (first 8 bytes for a leaf B-tree page) and the cell pointers:

- **`0d`**: This flag indicates that the page is a **leaf B-tree page** for a table.
- **`00 00`**: The offset to the first free block. `00 00` indicates there are no freeblocks.
- **`00 05`**: The number of cells on this page. This matches the 5 student rows inserted.
- **`0f d0`**: The byte offset indicating where the cell content area begins.
- **`00`**: The number of fragmented free bytes (0).

Following the 8-byte header, we can observe the 16-bit integer cell pointer array. The first pointers are `0ff6`, `0fee`, `0fe2`, `0fd8`, `0fd0`. These specify the exact offsets within the page where each of the 5 rows is physically stored.

## Summary

SQLite stores tables natively inside a B-tree structure embedded within the single `.db` file. We verified that our table's root node resides on Page 2 (offset `0x1000`), starting with the `0d` signature confirming it is a leaf B-tree containing the actual payload data of the 5 inserted rows.
