# SQLite Page Layout Analysis

## Introduction

SQLite databases are divided into fixed-size pages.

For this database:

| Property | Value |
|---|---|
| Page Size | 4096 bytes |
| Total Pages | 7 |

SQLite stores all information using page-oriented storage.

Pages are organized internally as B-Tree nodes.

---

## SQLite Database Header

The first 100 bytes of page 1 contain the SQLite database header.

Hex dump:

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300
```

ASCII representation:

```text
SQLite format 3
```

Important header fields:

| Offset | Meaning |
|---|---|
| 0-15 | SQLite signature |
| 16-17 | Page size |
| 18 | Write version |
| 19 | Read version |

---

## General SQLite Page Structure

Each page contains four major regions:

```text
+----------------------+
| Page Header          |
+----------------------+
| Cell Pointer Array   |
+----------------------+
| Free Space           |
+----------------------+
| Cell Content Area    |
+----------------------+
```

---

## Page Header

Each B-Tree page begins with a page header.

Important header fields:

| Offset | Meaning |
|---|---|
| 0 | Page type |
| 1-2 | First freeblock |
| 3-4 | Number of cells |
| 5-6 | Cell content start |
| 7 | Fragmented free bytes |

---

## SQLite Page Types

SQLite uses different page types for B-Tree storage.

| Hex Value | Meaning |
|---|---|
| 0x05 | Interior Table B-Tree Page |
| 0x0D | Leaf Table B-Tree Page |

Interior pages contain:
- child page pointers
- separator keys

Leaf pages contain:
- actual row records

---

## Cell Pointer Array

The cell pointer array stores offsets to records inside the page.

Example:

```text
0fa0
0f80
0f60
```

Each value points to a cell location within the page.

---

## Free Space Region

The free space region exists between:
- cell pointer array
- cell content area

SQLite uses this region for:
- inserting new records
- updating rows
- page balancing

---

## Cell Content Area

The cell content area stores:
- records
- payload data
- rowids
- serial type information

Cell content grows upward from the bottom of the page.

Cell pointers grow downward from the top.

This design improves space utilization inside pages.

---

## Summary

SQLite organizes storage using fixed-size pages and B-Tree structures.

Each page contains:
- metadata
- pointer arrays
- free space
- records

This structure enables:
- efficient searching
- balanced traversal
- compact storage
- reduced disk I/O