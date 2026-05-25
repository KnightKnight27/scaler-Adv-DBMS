# Lab 4 — SQLite 3 Hex Dump Analysis

> **Course:** Advanced DBMS
> **Author:** Krritin Keshan (24BCS10122)

This lab explores the **physical file structure** of a SQLite 3 database by analyzing its raw hex dump. The focus is on understanding the **B-tree implementation**, **page layout**, and how internal pointers enable fast row lookups.

---

## Table of Contents
1. [Files in this Lab](#files-in-this-lab)
2. [Setup & Database Generation](#setup--database-generation)
3. [Addresses and Offsets](#1-addresses-and-offsets)
4. [Navigation of Fields in the Page](#2-navigation-of-fields-in-the-page)
5. [B-Tree Nodes and Lookup](#3-b-tree-nodes-and-lookup)
6. [Leaf Node Structure (The Payload)](#4-leaf-node-structure-the-payload)
7. [Key Takeaways](#key-takeaways)

---

## Files in this Lab
| File | Description |
|------|-------------|
| `my_database.db` | The SQLite 3 database file (28 KB) used for analysis. |
| `hexdump.txt` | The full `xxd` hex dump of the database. |
| `README.md` | This document — a walkthrough of the dump. |

---

## Setup & Database Generation

We created a test database (`my_database.db`) with a `users` table:

```sql
CREATE TABLE users (
    id          INTEGER PRIMARY KEY,
    name        TEXT,
    description TEXT
);
```

To force the creation of an **Interior Table B-Tree Node** (a page that points to other pages), we needed to overflow the root page. Since SQLite uses a default page size of **4096 bytes**, we inserted **20 rows** with ~1000 bytes of data each. This forced the root page to split and create an interior node pointing to multiple leaf nodes.

The hex dump was generated with:

```bash
xxd my_database.db > hexdump.txt
```

---

## 1. Addresses and Offsets

In SQLite, the file is divided into fixed-size **Pages** (default `4096` bytes / `0x1000`).

- Pages are **1-indexed**. Page 1 contains the 100-byte database header and the root of the `sqlite_schema` table.
- The byte offset of any page is given by:

  > **`Offset = (Page_Number - 1) × Page_Size`**

| Page | Offset (decimal) | Offset (hex) |
|------|------------------|--------------|
| 1    | 0                | `0x0000`     |
| 2    | 4096             | `0x1000`     |
| 3    | 8192             | `0x2000`     |
| 4    | 12288            | `0x3000`     |
| 5    | 16384            | `0x4000`     |

Inside a page, addresses are stored as **2-byte offsets relative to the start of the page**.

---

## 2. Navigation of Fields in the Page

Let's examine **Page 2**, which serves as the root interior B-tree node for our `users` table. The first 16 bytes of Page 2 at offset `0x1000`:

```text
00001000: 0500 0000 040f ec00 0000 0007 0ffb 0ff6  ................
```

### The Page Header
B-tree pages have a header (**8 bytes** for leaf, **12 bytes** for interior):

| Bytes | Value | Meaning |
|-------|-------|---------|
| `0x05` | 1 byte | **Page Flag** — `0x05` = Interior Table B-Tree Page (`0x0d` = Leaf) |
| `0x00 00` | 2 bytes | First freeblock offset (`0` = none) |
| `0x00 04` | 2 bytes | **Number of cells** on this page → **4 cells** |
| `0x0f ec` | 2 bytes | **Start of cell content area** → byte `4076` into the page |
| `0x00` | 1 byte | Number of fragmented free bytes |
| `0x00 00 00 07` | 4 bytes | **Right-most pointer** → Page 7 (holds rows with highest rowids) |

### The Cell Pointer Array
Immediately after the header is the **Cell Pointer Array** — 2-byte offsets that locate each cell within the page.

Looking at the bytes `0ffb 0ff6 0ff1 0fec`:

| Cell # | Offset |
|--------|--------|
| 0 | `0x0ffb` |
| 1 | `0x0ff6` |
| 2 | `0x0ff1` |
| 3 | `0x0fec` |

> Cells are populated from the **end of the page backwards**, which is why the cell content area sits near the end of the 4096-byte page. The header and pointer array grow from the top, while cells grow from the bottom — meeting in the middle.

---

## 3. B-Tree Nodes and Lookup

### Interior B-Tree Structure (Pointers)

Let's look at the actual cells (pointers) in the interior node (Page 2). We check offset `0x1000 + 0x0fec = 0x1fec` (the cell content area):

```text
00001fea: 00 00 00 06 10 00 00 00 05 0c 00 00 00 04  ................
00001ffa: 08 00 00 00 03 04                          ......
```

Realigning to the offsets from the pointer array:

| Cell | Offset | Raw Bytes | Left Child | Max Key |
|------|--------|-----------|------------|---------|
| 0 | `0x0ffb` | `00 00 00 03 04` | **Page 3** | `4` |
| 1 | `0x0ff6` | `00 00 00 04 08` | **Page 4** | `8` |
| 2 | `0x0ff1` | `00 00 00 05 0c` | **Page 5** | `12` |
| 3 | `0x0fec` | `00 00 00 06 10` | **Page 6** | `16` |

And the **Right-most pointer** from the header (`0x00 00 00 07`) points to **Page 7**, which catches all keys `> 16`.

### Traversing a Lookup

Imagine we run:

```sql
SELECT * FROM users WHERE id = 10;
```

1. SQLite reads the schema and finds the `users` table root page is **Page 2**.
2. It reads the header of Page 2 → flag `0x05` → interior node.
3. It scans the cells linearly:
   - Cell 0 (max key 4) → `10 > 4` ❌ skip
   - Cell 1 (max key 8) → `10 > 8` ❌ skip
   - Cell 2 (max key 12) → `10 ≤ 12` ✅ **match!**
4. SQLite follows the left-child pointer → **Page 5** (`0x00 00 00 05`).
5. It computes the file offset for Page 5 → `(5 − 1) × 4096 = 0x4000`.
6. At offset `0x4000`, it finds a Leaf Node (`0x0d`), reads its cell pointer array, and locates the exact payload for `id = 10`.

---

## 4. Leaf Node Structure (The Payload)

Let's look at **Page 3** (offset `0x2000`), which holds rows 1 through 4.

Header:
```text
00002000: 0d00 0000 0400 2800 0c0a ...
```
- `0x0d` → **Leaf Table B-Tree Page**

Examining the first cell payload located at offset `0x2c0a` (inside Page 3):

```text
00002c0a: 8773 0105 0019 8f5d 5573 6572 2031 3937  .s.....]User 197
```

| Bytes | Field | Decoded Value |
|-------|-------|---------------|
| `87 73` | Payload Size (varint) | **1011 bytes** |
| `01` | Rowid (varint) | **`id = 1`** |
| `05` | Header size (varint) | Record header is **5 bytes** |
| `00` | Column 0 (id) type | Aliased to rowid → **0 bytes in payload** |
| `19` | Column 1 (name) type | String of length **6** |
| `8f 5d` | Column 2 (description) type | String of length **1000** |
| `55 73 65 72 20 31` | Payload data | `"User 1"` (followed by the 1000-byte description) |

---

## Key Takeaways

- **SQLite is just B-trees + pages.** The entire database is organized into 4096-byte pages, with B-trees indexing every table.
- **Pages grow from both ends.** Header and cell pointers grow downward from the top; cell content grows upward from the bottom.
- **Interior nodes contain (pointer, key) pairs** — they direct the search, but never store row data.
- **Leaf nodes contain the actual row payload**, encoded as varints with a record header describing each column's type and size.
- **Lookups are O(log n)** thanks to the B-tree fanout; each interior cell halves (or more) the search space.

---

> *Generated as part of Lab 4 — Advanced DBMS coursework.*
