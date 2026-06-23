# SQLite Storage Inspection – Lab 4

## Objective

The purpose of this lab was to explore how SQLite stores table data internally inside a database file.  
To do this, I created a small database, inserted records, and examined the raw binary layout using hexadecimal output.

---

# Database Setup

First, a new SQLite database named `lab4.db` was created.

## Table Definition

```sql
CREATE TABLE t (
    id INTEGER PRIMARY KEY,
    name TEXT
);
```

## Sample Records

```sql
INSERT INTO t(name)
VALUES
    ('alice'),
    ('bob'),
    ('carol'),
    ('dave');
```

---

# File Examination

After populating the database, the file contents were inspected using the `xxd` command.

Additional SQLite metadata was also checked, including:

- database page size
- total number of pages
- schema information
- root page number of the table

---

# Observations

The database file begins with the text:

```text
SQLite format 3
```

which confirms that the file is recognized as a valid SQLite database.

Some important details observed:

| Property | Value |
|---|---|
| Page Size | 4096 bytes |
| Total Pages | 2 |
| Root Page of Table `t` | 2 |

The second page represents the root B-tree page for the table.

---

# Raw Hex Dump

The following bytes were captured from the beginning of page 2:

```text
00001000: 0d00 0000 040f db00 0ff6 0fee 0fe4 0fdb
```

---

# Page Header Interpretation

The header values indicate the following:

| Bytes | Meaning |
|---|---|
| `0d` | Table leaf B-tree page |
| `00 00` | No freeblock present |
| `00 04` | Total number of cells = 4 |
| `0f db` | Start of cell content area |

The remaining values are offsets pointing to the individual table cells.

Since four records were inserted earlier, the page structure correctly reports four cells.

---

# Final Notes

This experiment shows that SQLite stores both schema information and table data directly inside a single database file.

The table data is organized using a B-tree structure, and for this database the root node begins at page 2.