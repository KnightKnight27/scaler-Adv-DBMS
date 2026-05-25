# Cell Pointer Array Analysis

## Introduction

Each SQLite B-Tree page contains a cell pointer array.

The cell pointer array stores offsets to records inside the page.

This mechanism allows SQLite to:
- locate rows efficiently
- keep records sorted
- move records without reorganizing the entire page

---

## Cell Pointer Array Structure

General structure:

```text
+----------------------+
| Page Header          |
+----------------------+
| Cell Pointer Array   |
| 0x0fa0               |
| 0x0f80               |
| 0x0f60               |
+----------------------+
```

Each entry is a 2-byte offset.

---

## Example Cell Pointers

Example values:

```text
0fa0
0f80
0f60
```

Interpretation:

| Pointer | Meaning |
|---|---|
| 0x0fa0 | Record begins at offset 0x0fa0 |
| 0x0f80 | Record begins at offset 0x0f80 |
| 0x0f60 | Record begins at offset 0x0f60 |

---

## Offset Calculations

Page size:

```text
4096 bytes
```

Offset formula:

```text
absolute_offset = page_offset + cell_offset
```

Example:

```text
Page 2 offset:
4096

Cell offset:
0x0fa0

Absolute location:
4096 + 0x0fa0
```

---

## Purpose of Cell Pointers

Cell pointers provide:
- sorted record access
- fast traversal
- compact storage
- easier record relocation

SQLite can rearrange records by changing pointers
instead of rewriting entire pages.

---

## Cell Growth Direction

Inside the page:

- cell pointers grow downward
- cell content grows upward

This creates free space in the middle of the page.

Example:

```text
+----------------------+
| Header               |
+----------------------+
| Pointer Array ↓      |
|                      |
|      Free Space      |
|                      |
| Record Data ↑        |
+----------------------+
```

---

## Relationship with B-Trees

Cell pointers are heavily used during:
- B-Tree traversal
- row lookups
- page balancing
- insertions
- deletions

Leaf pages use pointers to locate actual rows.

Interior pages use pointers for child navigation.

---

## Summary

The cell pointer array is a critical SQLite page structure.

It enables:
- efficient navigation
- compact storage
- fast row access
- dynamic page organization

Cell pointers act as the connection between
page metadata and actual record locations.