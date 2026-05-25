# Lab 4 — Inspecting a SQLite Database with xxd

## Overview

The goal of this lab was to create a real SQLite database, insert some data, and then use `xxd` to open the raw file and actually see how SQLite lays the data out on disk. I wanted to understand what a B-tree page looks like at the byte level rather than just reading about it.

---

## Step 1 — Creating the Database

I created a database called `lab4.db` using the `sqlite3` command-line tool and added a simple table to store student records:

```sql
CREATE TABLE students (
    id    INTEGER PRIMARY KEY,
    name  TEXT,
    grade TEXT
);
```

Then I inserted four rows:

```sql
INSERT INTO students (name, grade) VALUES
    ('Alice',   'A'),
    ('Bob',     'B+'),
    ('Charlie', 'A-'),
    ('Diana',   'A');
```

Nothing fancy — just enough rows so the B-tree page would have something interesting to show.

---

## Step 2 — Checking Basic File Info

Before looking at raw bytes I queried a few SQLite pragmas to understand the file structure at a high level:

```sql
PRAGMA page_size;   -- 4096
PRAGMA page_count;  -- 2
SELECT name, rootpage FROM sqlite_master WHERE type = 'table';
-- students | 2
```

So the file is exactly **two pages**, each **4096 bytes** wide.

| Item | Value |
|---|---|
| Page size | 4096 bytes |
| Total pages | 2 |
| Page 1 | Schema / sqlite_master table |
| Page 2 | `students` table (root page) |

Page 1 is always reserved by SQLite for internal bookkeeping (the 100-byte file header plus the schema table). The actual `students` data lives on page 2, which starts at byte offset `0x1000`.

---

## Step 3 — Running xxd

I dumped the entire file to `hexdump.txt`:

```bash
xxd lab4.db > hexdump.txt
```

To zoom in on just the file header I looked at the first few lines:

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0c40 2020 0000 0002 0000 0002  .....@  ........
```

The very first 16 bytes spell out `SQLite format 3` followed by a null byte — that is the magic string SQLite uses so that you can immediately tell what kind of file you are looking at. Bytes at offset `0x10` and `0x11` (`10 00` big-endian) give the page size: `0x1000 = 4096`. Bytes `0x1c`–`0x1f` encode the page count as `0x00000002 = 2`, which matches the pragma output.

---

## Step 4 — Looking at Page 2 (the B-tree Page)

Page 2 starts at offset `0x1000`. Here is what `xxd` shows at the top of that page:

```
00001000: 0d00 0000 040f c200 0fe8 0fdd 0fce 0fc2  ................
```

SQLite defines an 8-byte B-tree page header for leaf pages. I read it byte by byte:

| Bytes | Hex | Meaning |
|---|---|---|
| 0x1000 | `0d` | Page type — `0x0d` means **table leaf** |
| 0x1001–0x1002 | `00 00` | First freeblock offset — `0` means no free blocks |
| 0x1003–0x1004 | `00 04` | **Cell count = 4** (one per row inserted) |
| 0x1005–0x1006 | `0f c2` | Cell content area starts at offset `0x0fc2` from page start |
| 0x1007 | `00` | Fragmented free bytes = 0 |

Right after the header, starting at `0x1008`, are the **cell pointer array** entries. Each pointer is a 2-byte offset pointing to where the actual cell data sits within the page:

| Pointer # | Hex | Offset in page | Row |
|---|---|---|---|
| 1 | `0f e8` | `0x0fe8` | Alice (rowid 1) |
| 2 | `0f dd` | `0x0fdd` | Bob (rowid 2) |
| 3 | `0f ce` | `0x0fce` | Charlie (rowid 3) |
| 4 | `0f c2` | `0x0fc2` | Diana (rowid 4) |

Notice the pointers go from higher offsets down to lower ones. That is because SQLite grows cell content from the **end of the page upward**, while the header and pointer array grow downward from the start. The two regions meet somewhere in the middle (currently all those zeros in the dump).

---

## Step 5 — Reading a Single Cell

To confirm the structure I jumped to offset `0x1fc2` (page 2 base `0x1000` + in-page offset `0x0fc2`) and looked at the raw bytes there:

```
00001fc2: 0a 04 04 00 17 0f 44 69 61 6e 61 41 ...
```

This is the cell for Diana. Decoding it:

- `0a` — **payload length** varint = 10 bytes of payload
- `04` — **rowid** varint = 4 (Diana's integer primary key)
- `04 00 17 0f` — **record header**: header length = 4, then two type codes: `0x17` = TEXT of length `(0x17 - 13) / 2 = 5` → "Diana"; `0x0f` = TEXT of length `(0x0f - 13) / 2 = 1` → "A"
- `44 69 61 6e 61` — ASCII for `Diana`
- `41` — ASCII for `A`

That matches exactly what I inserted. The other cells follow the same layout — just with different rowids and string lengths.

---

## Step 6 — Verifying Cell Content for All Rows

Here is a summary of what shows up in the tail end of the page:

```
00001fc2: 0a04 0400 170f 4469 616e 6141  ......DianaA    <- rowid 4, Diana, A
00001fce: 0d03 0400 1b11 4368 6172 6c69 6541 2d    ....CharlieA-   <- rowid 3, Charlie, A-
00001fdd: 0913 1142 6f62 422b                       ...BobB+        <- rowid 2, Bob, B+
00001fe8: 0a01 0400 170f 416c 6963 6541            ......AliceA    <- rowid 1, Alice, A
```

All four rows are there. SQLite stores them in **insertion order within the page** (not sorted by rowid at the byte level in a leaf page — the cell pointer array is what the engine uses to locate them).

---

## What I Learned

- A SQLite file is just a sequence of fixed-size pages. There is no hidden complexity at the file level once you know where each page starts.
- The first byte of any B-tree page tells you its type. `0x0d` is table leaf, `0x05` would be table interior, etc.
- The cell pointer array is the key index inside a leaf page — it lets the engine jump directly to any cell without scanning the whole page.
- Cell content is stored in a **record format** that starts with a varint header describing the types of each column. Once you know that, you can decode any row without needing the schema at hand.
- The large block of zeros between the cell pointer array and the cell content area is just unused space inside the page. As more rows are inserted it would fill up and eventually trigger a page split.

---

## Files in This Submission

| File | Description |
|---|---|
| `lab4.db` | The actual SQLite database (binary) |
| `hexdump.txt` | Full `xxd` output of `lab4.db` for reference |
| `README.md` | This write-up |