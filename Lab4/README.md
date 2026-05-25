# SQLite Database Internals - Lab 4

## Introduction

In this lab, I explored how SQLite stores data internally by creating a small database, inserting records, and inspecting the database file using hexadecimal analysis tools.

---

## Procedure

### Step 1: Create Database

A SQLite database file named `lab4.db` was created.

### Step 2: Create Table

```sql
CREATE TABLE t (
    id INTEGER PRIMARY KEY,
    name TEXT
);
```

### Step 3: Insert Records

```sql
INSERT INTO t(name)
VALUES ('alice'),
       ('bob'),
       ('carol'),
       ('dave');
```

### Step 4: Inspect Database File

The database file was examined using the `xxd` command to view its binary contents in hexadecimal format.

### Step 5: Verify SQLite Information

The following details were checked:

- Database page size
- Total number of pages
- Schema information
- Root page details

---

## Observations

### Database Header

The file begins with:

```text
SQLite format 3
```

This confirms that the file is a valid SQLite database.

### Database Information

| Property | Value |
|----------|-------|
| Page Size | 4096 bytes |
| Page Count | 2 |
| Root Page of Table `t` | 2 |

---

## B-Tree Page Details

Page number `2` starts with the byte:

```text
0d
```

This value represents a **table leaf B-tree page** in SQLite.

---

## Hex Dump

```text
00001000: 0d 00 00 00 04 0f db 00 0f f6 0f ee 0f e4 0f db
```

---

## Interpretation of Bytes

| Bytes | Meaning |
|-------|---------|
| `0d` | Table leaf B-tree page |
| `00 00` | No free blocks |
| `00 04` | Total number of cells = 4 |
| `0f db` | Start of cell content area |
| Remaining values | Cell pointer array |

---

## Conclusion

This experiment demonstrates how SQLite stores table data directly inside the database file using B-tree structures.

The root page for table `t` is page `2`, and the hexadecimal analysis confirms that the inserted rows are physically stored within the database pages.