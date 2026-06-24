# Lab 4: SQLite Database File Analysis

## Overview

In this lab, I created and examined a **SQLite database file** to understand how SQLite stores table data internally. Using SQLite commands and the `xxd` utility, I inspected both the database structure and the raw hexadecimal contents of the file.

---

## Steps Performed

### 1. Database Creation

A new SQLite database file named **`lab4.db`** was generated.

### 2. Table Setup

Inside the database, a table named **`t`** was created with the following schema:

```sql
CREATE TABLE t (
    id INTEGER PRIMARY KEY,
    name TEXT
);
```

### 3. Data Insertion

Several sample records were inserted into the table:

```sql
INSERT INTO t(name)
VALUES ('alice'), ('bob'), ('carol'), ('dave');
```

### 4. File Inspection

The database file was examined using the `xxd` command to view its hexadecimal representation.

### 5. Database Metadata Verification

Additional SQLite commands were used to check:

- Database page size
- Total number of pages
- Table schema details
- Root page location

---

## Observations

### Database Header

The beginning of the file contains the text:

```text
SQLite format 3
```

This confirms that the file is a valid SQLite database.

### Page Information

- **Page Size:** `4096 bytes`
- **Total Pages:** `2`
- **Root Page of table `t`:** `2`

---

## Hex Dump from Table Root Page

The hexadecimal bytes from page 2 are shown below:

```text
00001000: 0d 00 00 00 04 0f db 00 0f f6 0f ee 0f e4 0f db
```

---

## Interpretation of Page Structure

Breaking down the important bytes:

- **`0d`** → Indicates a **table leaf B-tree page**
- **`00 00`** → No free blocks are present
- **`00 04`** → The page contains **4 cells (rows)**
- **`0f db`** → Starting offset of the cell content area
- Remaining values represent **cell pointers** pointing to individual row records

---

## Conclusion

This experiment demonstrates that SQLite stores both database metadata and actual table records within a single database file.

The table data is organized using a **B-tree structure**, with the table’s root located on **page 2**. The page header information correctly reflects the four inserted rows, confirming how SQLite manages internal storage efficiently.