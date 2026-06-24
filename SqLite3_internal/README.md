# Lab 4 — SQLite 3 Hex Dump Analysis

> **Course:** Advanced DBMS
> **Author:** Kanan Arora

The goal of this lab was to stop treating a `.db` file as a black box and actually read it byte by byte. SQLite stores everything — schema, rows, indexes — as B-trees laid out across fixed-size pages. By dumping a small database with `xxd` and walking through the bytes, you can watch a `SELECT` traverse from the root page down to the exact row.

---

## Table of Contents
1. [Files in this Lab](#files-in-this-lab)
2. [Building the Database](#building-the-database)
3. [Pages, Offsets and Addressing](#1-pages-offsets-and-addressing)
4. [Reading a Page Header](#2-reading-a-page-header)
5. [The Interior Node and a Lookup](#3-the-interior-node-and-a-lookup)
6. [Inside a Leaf Node](#4-inside-a-leaf-node)
7. [Key Takeaways](#key-takeaways)

---

## Files in this Lab
| File | What it is |
|------|------------|
| `my_database.db` | The SQLite 3 database (28 KB, 7 pages) we dissected. |
| `hexdump.txt` | Full `xxd` dump of that file. |
| `README.md` | This walkthrough. |

---

## Building the Database

A single root page wouldn't show us anything interesting — to see an **interior** B-tree node we needed the table to outgrow one page and split. The trick is to make the rows fat. Each row carries a ~1000-byte `description`, and with 20 of them the table can no longer fit on one page, so SQLite promotes a new interior node above the leaves.

```sql
PRAGMA page_size = 4096;

CREATE TABLE users (
    id          INTEGER PRIMARY KEY,
    name        TEXT,
    description TEXT
);

-- 20 rows, each description ~1000 bytes (hex of a 500-byte blob)
INSERT INTO users (id, name, description)
SELECT n, 'User ' || n, hex(zeroblob(500))
FROM (WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < 20) SELECT n FROM seq);
```

Then the dump:

```bash
xxd my_database.db > hexdump.txt
```

The result is a 7-page file. Page 1 holds the database header and schema; page 2 became the **interior** node for `users`; pages 3–7 are the **leaves** holding the actual rows. You can confirm the table's root page yourself:

```bash
sqlite3 my_database.db "SELECT name, rootpage FROM sqlite_schema;"
-- users|2
```

---

## 1. Pages, Offsets and Addressing

The file is just a stack of fixed-size pages — `4096` bytes (`0x1000`) here. Pages are numbered from **1**, and page 1 is special because its first 100 bytes are the global file header (`SQLite format 3\0`).

To jump to any page, the math is trivial:

> **Offset = (Page − 1) × 4096**

| Page | Offset (dec) | Offset (hex) | Role |
|------|--------------|--------------|------|
| 1 | 0 | `0x0000` | File header + schema |
| 2 | 4096 | `0x1000` | Interior node (`users`) |
| 3 | 8192 | `0x2000` | Leaf — rowids 1–4 |
| 5 | 16384 | `0x4000` | Leaf — rowids 9–12 |
| 7 | 24576 | `0x6000` | Leaf — highest rowids |

Within a page, every pointer is a **2-byte offset measured from the start of that page**, not from the start of the file.

---

## 2. Reading a Page Header

Here are the first bytes of page 2 (offset `0x1000`):

```text
00001000: 0500 0000 040f e000 0000 0007 0fef 0fea  ................
00001010: 0fe5 0fe0 ...
```

An interior page header is **12 bytes** (a leaf header is only 8 — it has no right-most pointer). Decoding it:

| Bytes | Field | Value |
|-------|-------|-------|
| `05` | Page type | `0x05` = **interior table** B-tree (`0x0d` would be a leaf) |
| `00 00` | First freeblock | `0` — no free space |
| `00 04` | Cell count | **4 cells** |
| `0f e0` | Cell content start | byte **4064** into the page |
| `00` | Fragmented free bytes | `0` |
| `00 00 00 07` | Right-most child | **page 7** |

Right after the 12-byte header comes the **cell pointer array** — one 2-byte entry per cell, telling you where each cell lives inside the page. From the dump: `0fef 0fea 0fe5 0fe0`.

| Cell | Pointer |
|------|---------|
| 0 | `0x0fef` |
| 1 | `0x0fea` |
| 2 | `0x0fe5` |
| 3 | `0x0fe0` |

Notice the cells sit near the *bottom* of the page (around `0xfe0`–`0xfff`) while the header and pointer array sit at the *top*. That's deliberate: SQLite fills a page from both ends toward the middle. Pointers grow downward, cell data grows upward, and the gap between them is the free space.

---

## 3. The Interior Node and a Lookup

An interior cell is dead simple: a **4-byte child page number** followed by a **varint key**. It stores no row data — just signposts. Reading the four cells at their offsets:

```text
00001fe0: 0000 0006 10 ...   (cell 3)
          0000 0005 0c ...   (cell 2)
          0000 0004 08 ...   (cell 1)
          0000 0003 04 ...   (cell 0)
```

| Cell | Bytes | Points to | Key (max rowid in that subtree) |
|------|-------|-----------|----------------------------------|
| 0 | `00 00 00 03  04` | page 3 | ≤ 4 |
| 1 | `00 00 00 04  08` | page 4 | ≤ 8 |
| 2 | `00 00 00 05  0c` | page 5 | ≤ 12 |
| 3 | `00 00 00 06  10` | page 6 | ≤ 16 |
| — | right-most ptr | page 7 | > 16 |

So the rowid space is partitioned: 1–4 → page 3, 5–8 → page 4, 9–12 → page 5, 13–16 → page 6, and anything above 16 → page 7.

### Tracing `SELECT * FROM users WHERE id = 10`

1. The schema says `users` lives on **page 2**.
2. Page 2's type byte is `0x05`, so it's an interior node — we won't find the row here, only a direction.
3. Walk the keys until one is ≥ 10:
   - key 4 → `10 > 4`, keep going
   - key 8 → `10 > 8`, keep going
   - key 12 → `10 ≤ 12`, **stop**
4. That cell points to **page 5**.
5. Page 5 is at `(5 − 1) × 4096 = 0x4000`.
6. Jump there, confirm the leaf flag `0x0d`, read its cell pointers, and pull the record for rowid 10.

Two pages read, no scanning — that's the B-tree paying off.

---

## 4. Inside a Leaf Node

Page 3 (offset `0x2000`) holds rowids 1–4. Its header:

```text
00002000: 0d00 0000 0400 1c00 ...
```

`0x0d` = leaf table page, `00 04` = 4 cells, `00 1c` = content starts at byte 28. The first cell pointer is `0x0bfe`, so let's decode the record sitting there:

```text
0xbfe: 87 73 01 05 00 19 8f 5d 55 73 65 72 20 31 ...
```

A leaf cell is `[payload size][rowid][record]`, and the record itself is `[header size][serial types...][column data...]`. Everything sized is a **varint** (7 bits per byte, high bit = "continue").

| Bytes | Field | Decoded |
|-------|-------|---------|
| `87 73` | Payload size (varint) | **1011 bytes** |
| `01` | Rowid (varint) | **1** |
| `05` | Record header size | 5 bytes of type info follow |
| `00` | Col `id` serial type | `0` → value *is* the rowid, stored nowhere else |
| `19` | Col `name` serial type | `25` → text, length `(25−13)/2 =` **6** |
| `8f 5d` | Col `description` serial type | `2013` → text, length `(2013−13)/2 =` **1000** |
| `55 73 65 72 20 31` | First column data | `"User 1"` |

After `"User 1"` come the 1000 bytes of the description string. Add it up — 6 + 1000 plus the small header — and you land back at the 1011-byte payload the cell announced up front.

A couple of details worth pulling out:
- The `id` column takes **zero bytes** in the payload. Because it's `INTEGER PRIMARY KEY`, SQLite aliases it to the rowid and refuses to store it twice. Serial type `0` is how it records "this is NULL / lives elsewhere."
- Text serial types are always **odd and ≥ 13**; you recover the byte length with `(type − 13) / 2`. Blobs use the even numbers `≥ 12`.

---

## Key Takeaways

- **A SQLite database is B-trees all the way down.** Fixed 4096-byte pages, one B-tree per table, navigated by page numbers.
- **Pages are packed from both ends.** Header and cell pointers grow down from the top; cell payloads grow up from the bottom; free space is whatever's left in the middle.
- **Interior nodes only point.** Each cell is a `(child page, key)` pair that narrows the search — no row data ever lives there.
- **Leaf nodes hold the real records**, encoded as varints with a record header that spells out each column's type and length.
- **Integer primary keys are free.** They're folded into the rowid, so the column itself costs zero bytes on disk.
- **The whole point is fewer reads.** Our `id = 10` lookup touched two pages instead of scanning twenty rows — that's `O(log n)` in action.
