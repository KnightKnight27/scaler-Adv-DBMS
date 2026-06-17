# Lab 4 — SQLite 3 Hex Dump Analysis

> **Course:** Advanced DBMS
> **Author:** Gauri Shukla (24BCS10115)

This lab explores the **physical file structure** of a SQLite 3 database by analyzing its raw hex dump. The goal is to understand the B-tree page layout, page headers, cell pointer arrays, interior vs leaf nodes, and how SQLite actually stores and retrieves row data at the byte level.

---

## Table of Contents

1. [Files in this Lab](#files-in-this-lab)
2. [Setup & Database Generation](#setup--database-generation)
3. [Addresses and Offsets](#1-addresses-and-offsets)
4. [SQLite File Header](#2-sqlite-file-header)
5. [Navigation of Fields in the Page](#3-navigation-of-fields-in-the-page)
6. [B-Tree Nodes and Lookup](#4-b-tree-nodes-and-lookup)
7. [Leaf Node Structure (The Payload)](#5-leaf-node-structure-the-payload)
8. [Key Takeaways](#key-takeaways)

---

## Files in this Lab

| File | Description |
|------|-------------|
| `books.db` | SQLite 3 database (40 KB) used for analysis |
| `hexdump.txt` | Full `xxd` hex dump of the database |
| `README.md` | This document — a walkthrough of the dump |

---

## Setup & Database Generation

Two tables were created: a `books` table (20 rows, the main subject of analysis) and an `authors` table (5 rows).

```sql
CREATE TABLE books (
    id      INTEGER PRIMARY KEY,
    title   TEXT,
    summary TEXT
);

CREATE TABLE authors (
    id   INTEGER PRIMARY KEY,
    name TEXT,
    bio  TEXT
);
```

To force the creation of an **Interior Table B-Tree Node** (a page that only holds pointers to other pages, not actual row data), each book row was given a `summary` of approximately **1000 bytes**. A single 4096-byte page can hold roughly 3–4 such rows, so inserting 20 rows forces the root page to **split** into an interior node with multiple child leaf pages.

The hex dump was generated with:

```bash
xxd books.db > hexdump.txt
```

---

## 1. Addresses and Offsets

SQLite divides every database file into fixed-size **pages** (default: **4096 bytes** = `0x1000`).

- Pages are **1-indexed**. Page 1 always begins at byte 0 and contains the 100-byte file header.
- The byte offset of any page is:

  > **`Offset = (Page_Number − 1) × Page_Size`**

| Page | Role | Offset (decimal) | Offset (hex) |
|------|------|------------------|--------------|
| 1 | `sqlite_schema` (leaf) | 0 | `0x0000` |
| 2 | `books` table root **(interior)** | 4 096 | `0x1000` |
| 3 | `authors` table root (leaf) | 8 192 | `0x2000` |
| 4 | `books` leaf — rows 1–3 | 12 288 | `0x3000` |
| 5 | `books` leaf — rows 4–6 | 16 384 | `0x4000` |
| 6 | `books` leaf — rows 7–9 | 20 480 | `0x5000` |
| 7 | `books` leaf — rows 10–12 | 24 576 | `0x6000` |
| 8 | `books` leaf — rows 13–15 | 28 672 | `0x7000` |
| 9 | `books` leaf — rows 16–18 | 32 768 | `0x8000` |
| 10 | `books` leaf — rows 19–20 | 36 864 | `0x9000` |

Our database has **10 pages × 4096 bytes = 40 960 bytes** total.

---

## 2. SQLite File Header

The first **100 bytes** of every SQLite file is a global file header. Let's read it from the hex dump:

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0003 0000 000a  .....@  ........
00000020: 0000 0000 0000 0000 0000 0002 0000 0004  ................
```

| Offset | Bytes | Value | Meaning |
|--------|-------|-------|---------|
| `0x00–0x0F` | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | `"SQLite format 3\0"` | Magic string — identifies this as a SQLite file |
| `0x10–0x11` | `10 00` | **4096** | Page size in bytes |
| `0x12` | `01` | 1 | File format write version |
| `0x13` | `01` | 1 | File format read version |
| `0x18–0x1B` | `00 00 00 03` | **3** | Change counter — incremented on every write |
| `0x1C–0x1F` | `00 00 00 0a` | **10** | Database size in pages |
| `0x38–0x3B` | `00 00 00 01` | 1 | User version |
| `0x38–0x3B` | `00 00 00 04` | 4 | Schema format number |
| `0x38–0x3B` | `00 00 00 01` | 1 (UTF-8) | Text encoding |

---

## 3. Navigation of Fields in the Page

Let's examine **Page 2** at offset `0x1000` — the root of the `books` table B-tree, which is an **interior node**.

### The Page Header

```text
00001000: 0500 0000 060f e200 0000 000a 0ffb 0ff6  ................
00001010: 0ff1 0fec 0fe7 0fe2 0000 0000 ...
```

Interior B-tree pages have a **12-byte header** (leaf pages have 8 bytes — no rightmost pointer):

| Bytes | Value | Meaning |
|-------|-------|---------|
| `05` | 1 byte | **Page type** — `0x05` = Interior Table B-Tree; `0x0d` = Leaf |
| `00 00` | 2 bytes | First freeblock offset (`0` = none) |
| `00 06` | 2 bytes | **Number of cells** on this page → **6 cells** |
| `0f e2` | 2 bytes | **Start of cell content area** → byte offset `4066` within the page |
| `00` | 1 byte | Fragmented free bytes |
| `00 00 00 0a` | 4 bytes | **Rightmost pointer** → Page **10** (holds the highest-rowid rows) |

### The Cell Pointer Array

Immediately after the 12-byte header is the **cell pointer array** — a sequence of 2-byte offsets, one per cell. Each offset points to where the cell's data sits within the page.

From the hex dump bytes at `0x100C`:

```text
0ffb  0ff6  0ff1  0fec  0fe7  0fe2
```

| Cell # | Pointer (page-relative) | Absolute offset in file |
|--------|------------------------|------------------------|
| 0 | `0x0ffb` | `0x1ffb` |
| 1 | `0x0ff6` | `0x1ff6` |
| 2 | `0x0ff1` | `0x1ff1` |
| 3 | `0x0fec` | `0x1fec` |
| 4 | `0x0fe7` | `0x1fe7` |
| 5 | `0x0fe2` | `0x1fe2` |

> Cells are written from the **end of the page backwards**. The header and pointer array grow downward from the top; cell content grows upward from the bottom — meeting in the middle. This is why the cell content area (`0x0fe2` = offset 4066) is near the end of the 4096-byte page.

---

## 4. B-Tree Nodes and Lookup

### Interior Node Cells

Each cell in an interior node encodes a **(left-child-page-number, max-rowid)** pair — a 4-byte child page number followed by a varint rowid key. SQLite follows the **left child** pointer for any rowid ≤ the max-rowid encoded in that cell.

Looking at the cell content area (absolute `0x1fe2`–`0x1fff`):

```text
00001fe0: 0000 0000 0009 1200 0000 080f 0000 0007  ................
00001ff0: 0c00 0000 0609 0000 0005 0600 0000 0403  ................
```

Reading each cell:

| Cell | Ptr offset | Raw bytes | Left child page | Max rowid |
|------|-----------|-----------|-----------------|-----------|
| 0 | `0x0ffb` | `00 00 00 04 03` | **Page 4** | **3** |
| 1 | `0x0ff6` | `00 00 00 05 06` | **Page 5** | **6** |
| 2 | `0x0ff1` | `00 00 00 06 09` | **Page 6** | **9** |
| 3 | `0x0fec` | `00 00 00 07 0c` | **Page 7** | **12** |
| 4 | `0x0fe7` | `00 00 00 08 0f` | **Page 8** | **15** |
| 5 | `0x0fe2` | `00 00 00 09 12` | **Page 9** | **18** |

And the rightmost pointer from the header (`0x00 00 00 0a`) → **Page 10**, which holds all rows with `id > 18`.

### Traversing a Lookup

Suppose SQLite runs:

```sql
SELECT * FROM books WHERE id = 10;
```

1. SQLite reads `sqlite_schema` (Page 1) and finds `books` root page = **Page 2**.
2. It reads byte `0x1000` → `0x05` → this is an **interior node**.
3. It scans the cell pointer array linearly:
   - Cell 0: max rowid `3` → `10 > 3` ❌ skip
   - Cell 1: max rowid `6` → `10 > 6` ❌ skip
   - Cell 2: max rowid `9` → `10 > 9` ❌ skip
   - Cell 3: max rowid `12` → `10 ≤ 12` ✅ **match**
4. It follows the left-child pointer → **Page 7**.
5. File offset of Page 7: `(7 − 1) × 4096 = 24576 = 0x6000`.
6. At `0x6000`, byte = `0x0d` → **Leaf node**. It scans the leaf's cell pointer array to find the exact payload for `id = 10`.

---

## 5. Leaf Node Structure (The Payload)

Let's examine **Page 4** (offset `0x3000`), which holds `books` rows 1, 2, and 3.

### Page 4 Header

```text
00003000: 0d00 0000 0304 0700 0c00 07fb 0407 0000  ................
```

| Bytes | Value | Meaning |
|-------|-------|---------|
| `0d` | — | **Page type** = Leaf Table B-Tree |
| `00 00` | — | No freeblocks |
| `00 03` | — | **3 cells** (rows 1, 2, 3) |
| `04 07` | — | Cell content starts at byte `0x0407` = 1031 within the page |

### Cell Pointer Array (Page 4)

```
0c00   07fb   0407
```

| Cell # | Pointer | Rowid (decoded) |
|--------|---------|-----------------|
| 0 | `0x0c00` | 1 ("The Great Gatsby") |
| 1 | `0x07fb` | 2 ("To Kill a Mockingbird") |
| 2 | `0x0407` | 3 ("1984") |

### Decoding Row 1 — "The Great Gatsby"

Row 1 is at page-relative offset `0x0c00` → absolute file offset **`0x3c00`**.

```text
00003c00: 877d 0105 002d 8f5d 5468 6520 4772 6561  .}...-.]The Grea
00003c10: 7420 4761 7473 6279 5375 6d6d 6172 7920  t GatsbySummary
```

Reading each field:

| Bytes | Field | Decoded Value |
|-------|-------|---------------|
| `87 7d` | **Payload size** (varint) | **1021 bytes** |
| `01` | **Rowid** (varint) | **`id = 1`** |
| `05` | Record header size (varint) | Header is **5 bytes** long |
| `00` | Column 0 (`id`) serial type | `0` → aliased to rowid, **0 bytes stored** |
| `2d` | Column 1 (`title`) serial type | `45` → TEXT of length `(45−13)/2 =` **16 chars** |
| `8f 5d` | Column 2 (`summary`) serial type | `2013` → TEXT of length `(2013−13)/2 =` **1000 chars** |
| `54 68 65 20 47 72 65 61 74 20 47 61 74 73 62 79` | Payload data | `"The Great Gatsby"` |

#### How varints work

SQLite uses variable-length integers (**varints**) to save space. Each byte contributes 7 bits to the value; the high bit signals whether more bytes follow.

- `87 7d`: high bit of `87` = 1 (more bytes follow) → `(0x87 & 0x7f) << 7 | 0x7d` = `7 << 7 | 125 = 896 + 125 = 1021` ✓
- `8f 5d`: similarly → `(0x8f & 0x7f) << 7 | 0x5d` = `15 << 7 | 93 = 1920 + 93 = 2013` ✓

#### How TEXT serial types work

SQLite encodes TEXT length in the serial type field itself:

> **`serial_type = 2 × byte_length + 13`** (for TEXT)

So to decode: `byte_length = (serial_type − 13) / 2`

- Title: `(45 − 13) / 2 = 16` bytes → `"The Great Gatsby"` ✓
- Summary: `(2013 − 13) / 2 = 1000` bytes ✓

---

## Key Takeaways

- **SQLite is pages + B-trees.** Every database file is a flat array of 4096-byte pages, each storing a B-tree node.
- **Pages grow from both ends.** The header and cell pointer array expand downward from offset 0; cell content is packed upward from the end of the page.
- **Interior nodes hold (child-pointer, max-rowid) pairs** — they navigate the tree but store no row data.
- **Leaf nodes hold the actual payloads**, encoded with a compact record header that describes each column's type and byte length using varints.
- **SQLite uses varints everywhere** to minimize storage: payload sizes, rowids, and column type codes are all variable-length, using the high bit as a continuation flag.
- **Lookups are O(log n)**: each interior node scan narrows the search to one child page, so even millions of rows need only a handful of page reads.

---

> *Lab 4 — Advanced DBMS coursework, Scaler School of Technology*
