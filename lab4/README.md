# Lab 4: SQLite3 Hex Dump and B-Tree Analysis

**Name:** Ankit Kumar  
**Roll No:** 24BCS10189  
**Lab No:** 4

## Objective

Analyze the internal layout of a SQLite3 database file using a hexadecimal
dump. The focus is on the SQLite file header, page size, B-tree page type,
cell pointer array, and row storage.

## Database Used

The database `students.db` is created from `schema.sql`.

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    marks INTEGER NOT NULL
);

INSERT INTO students (name, marks) VALUES
('Ankit', 92),
('Rahul', 88),
('Sneha', 91),
('Aman', 76),
('Priya', 89);
```

## Commands

```bash
sqlite3 students.db < schema.sql
sqlite3 students.db "PRAGMA page_size; PRAGMA page_count; SELECT * FROM students;"
xxd -g 1 -c 16 students.db dump.txt
```

## Observations

| Item | Value |
| --- | --- |
| Database file | `students.db` |
| Table | `students` |
| Rows inserted | `5` |
| SQLite file signature | `SQLite format 3` |
| Page size | `4096` bytes |
| Page count | `2` |
| Total file size | `8192` bytes |

## SQLite File Header

The first 16 bytes of the file contain the SQLite database signature:

```text
53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

ASCII interpretation:

```text
SQLite format 3
```

The page size is stored at offset `0x10` in the database header. The bytes are:

```text
10 00
```

`0x1000` in decimal is `4096`, so each page in this database is 4096 bytes.

## Page Layout

SQLite stores table rows in B-tree pages. A page contains:

```text
+----------------------+
| Page header          |
+----------------------+
| Cell pointer array   |
+----------------------+
| Free space           |
+----------------------+
| Cell content area    |
+----------------------+
```

For this small database, page 1 contains the SQLite schema table and page 2
contains the `students` table rows.

## Table Leaf Page

Page 2 starts at file offset `0x1000`. The first bytes of page 2 are:

```text
0d 00 00 00 05 ...
```

Interpretation:

| Bytes | Meaning |
| --- | --- |
| `0d` | Table leaf B-tree page |
| `00 00` | First freeblock offset |
| `00 05` | Number of cells = 5 |

The value `0x0d` confirms that the page is a table leaf page. The cell count
is `5`, matching the five inserted rows.

## Cell Pointer Array

After the page header, SQLite stores two-byte cell pointers. These pointers
refer to row cells stored near the end of the page. SQLite grows the pointer
array from the beginning of the page and stores record payloads from the end
of the page backward.

This separation allows SQLite to locate rows quickly without scanning the
entire page byte by byte.

## B-Tree Lookup

For a table with an `INTEGER PRIMARY KEY`, SQLite uses the rowid as the B-tree
key. To find a row:

1. SQLite starts at the table root page.
2. It checks whether the page is an internal page or a leaf page.
3. For this small table, the root is already a leaf page.
4. SQLite uses the cell pointer array to locate candidate cells.
5. It decodes the rowid and payload to return the matching record.

## Conclusion

The hex dump confirms that SQLite stores databases in fixed-size pages and
uses B-tree pages to organize table rows. The `students` table fits in a
single table leaf page with five cells, and the cell pointer array gives the
offsets of the actual row records.
