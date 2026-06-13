# Lab 4 – SQLite B-Tree Analysis

## Objective

The objective of this lab is to create a SQLite database, insert records into a table, generate a hexadecimal dump of the database file, and analyze the internal B-tree structure used by SQLite to store table data.

## Methodology

### Step 1: Create Database

A database file named `lab4.db` was created using SQLite.

### Step 2: Create Table

The following table was created:

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT
);
```

### Step 3: Insert Records

Five student records were inserted:

```sql
INSERT INTO students(name)
VALUES
('Alice'),
('Bob'),
('Charlie'),
('David'),
('Eve');
```

### Step 4: Generate Hex Dump

The database file was dumped into hexadecimal format using:

```bash
xxd lab4.db > hexdump.txt
```

### Step 5: Retrieve Database Information

The following commands were executed:

```sql
PRAGMA page_size;
PRAGMA page_count;

SELECT rootpage
FROM sqlite_master
WHERE type='table'
AND name='students';
```

Results:

- Page Size: 4096 bytes
- Page Count: 2
- Root Page: 2

## B-Tree Analysis

Since the page size is 4096 bytes (0x1000), Page 2 starts at offset:

```text
0x1000
```

A portion of the hex dump at the beginning of Page 2 appears as:

```text
00001000: 0d00 0000 050f d000 0ff6 0fee 0fe2 0fd8
```

### Interpretation

| Value | Meaning |
|---------|---------|
| 0d | Leaf table B-tree page |
| 00 00 | No free blocks |
| 00 05 | Five cells (records) |
| 0f d0 | Start of cell content area |
| 00 | No fragmented free bytes |

The cell pointers:

```text
0ff6
0fee
0fe2
0fd8
0fd0
```

indicate the locations of the five student records within the page.

## Conclusion

SQLite stores table data using a B-tree structure inside the database file. The `students` table is stored on Page 2, which is a leaf B-tree page containing the five inserted records. The hex dump confirms the presence of the B-tree page header and the associated cell pointers.