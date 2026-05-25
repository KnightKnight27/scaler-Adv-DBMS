# SQLite B-Tree Analysis

## Introduction

SQLite stores relational tables internally using B+Trees.

B-Trees provide:
- efficient searching
- balanced structure
- logarithmic traversal complexity
- optimized disk access

---

## Root Pages

The root pages were identified using:

```sql
SELECT name, rootpage FROM sqlite_master;
```

Output:

| Table | Root Page |
|---|---|
| users | 2 |
| products | 3 |

---

## Page Offsets

SQLite pages are stored sequentially inside the database file.

Formula:

```text
offset = (page_number - 1) * page_size
```

Using page size 4096 bytes:

| Page | Offset |
|---|---|
| 1 | 0 |
| 2 | 4096 |
| 3 | 8192 |

---

## B-Tree Structure

SQLite B-Tree structure:

```text
                 [Root Page]
                /     |     \
           Page2   Page3   Page4
```

The root page acts as the entry point for lookups.

---

## Interior Pages

Interior pages contain:
- separator keys
- child page pointers

Interior pages do NOT contain complete records.

Interior page type:

```text
0x05
```

---

## Leaf Pages

Leaf pages store:
- actual rows
- payload data
- record headers
- rowids

Leaf page type:

```text
0x0D
```

Leaf pages are the final destination during lookups.

---

## Lookup Traversal

SQLite performs lookups using tree traversal.

Traversal process:

1. Start at root page
2. Compare search key
3. Follow child pointer
4. Reach leaf page
5. Locate matching row

Example query:

```sql
SELECT * FROM users WHERE id = 250;
```

---

## Advantages of B-Trees

Advantages:
- O(log n) lookup complexity
- balanced structure
- reduced disk reads
- scalable storage
- efficient insertions

---

## B+Tree Characteristics

SQLite specifically uses B+Tree-style storage.

Characteristics:
- records stored only in leaf pages
- interior nodes contain navigation keys
- linked leaf traversal possible
- optimized sequential scans

---

## Summary

SQLite organizes relational data using page-oriented B+Trees.

The structure consists of:
- root pages
- interior pages
- leaf pages

This design enables efficient query execution and scalable storage.