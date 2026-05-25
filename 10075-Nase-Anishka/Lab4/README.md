# LAB 4 — READING A SQLITE FILE WITH XXD

> Roll Number: **10075** &nbsp;&nbsp;|&nbsp;&nbsp; Name: **Nase Anishka**
>
> In Lab 2 I treated `sample.db` as a black box that `PRAGMA` knew how
> to explain. In this lab I opened the file in hex and actually
> identified the magic header, the database header fields, the B-tree
> page type bytes, the cell pointer array, and the row payloads — to
> prove to myself that what `PRAGMA page_size` reports is literally a
> two-byte field at offset 16 of the file.

---

# WHAT I DID

* Built a tiny SQLite database `lab4.db` with one table (`books`) and
  five rows of book data.
* Ran `xxd lab4.db > hexdump.txt` to get a complete hex dump.
* Compared `PRAGMA page_size`, `PRAGMA page_count`, and the
  `sqlite_schema` rows against the raw bytes I could see in the dump.
* Decoded the **database header**, the **sqlite_schema record**, and
  the **books table's leaf B-tree page** byte by byte.

---

# FILES IN THIS FOLDER

* `lab4.db` — the SQLite database file (8 KB, 2 pages of 4 KB each).
* `hexdump.txt` — full `xxd` dump of `lab4.db` (512 lines).
* `commands.sh` — one-shot script that recreates `lab4.db`, regenerates
  the hex dump, and prints the pragmas.
* `README.md` — this file.

---

# HOW TO REPRODUCE

```bash
bash commands.sh
```

Then look at `hexdump.txt` alongside the byte-by-byte explanation below.

---

# 1. CREATING THE DB

```sql
CREATE TABLE books (
  id     INTEGER PRIMARY KEY,
  title  TEXT NOT NULL,
  author TEXT NOT NULL
);

INSERT INTO books (title, author) VALUES
 ('The Pragmatic Programmer',     'Andrew Hunt'),
 ('Designing Data-Intensive Apps', 'Martin Kleppmann'),
 ('The C Programming Language',   'Kernighan and Ritchie'),
 ('Clean Code',                   'Robert C Martin'),
 ('Database Internals',           'Alex Petrov');
```

```text
$ ls -lh lab4.db
-rw-r--r--  1 anishkanase  staff   8.0K lab4.db
$ sqlite3 lab4.db "PRAGMA page_size; PRAGMA page_count;"
4096
2
```

So the file is **exactly 2 pages of 4096 bytes = 8192 bytes**. Page 1
holds the database header + the `sqlite_schema` table; page 2 holds the
`books` leaf B-tree.

---

# 2. THE DATABASE HEADER (FIRST 100 BYTES)

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0002 0000 0002  .....@  ........
00000020: 0000 0000 0000 0000 0000 0001 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
```

| Bytes (offset) | Hex value     | Meaning                                              |
|----------------|---------------|------------------------------------------------------|
| `0x00..0x0F`   | `5351 4c69 ... 3300` | The literal ASCII string `"SQLite format 3\0"` — magic header that identifies this file as a SQLite 3 database. |
| `0x10..0x11`   | `1000`        | **Page size in bytes**, big-endian. `0x1000 = 4096`. This is exactly what `PRAGMA page_size` reports. |
| `0x12`         | `01`          | File format write version (1 = legacy journal). |
| `0x13`         | `01`          | File format read version (1). |
| `0x14`         | `0c`          | Reserved bytes at end of each page. macOS' system sqlite3 sets this to **12** by default, so the usable region of each 4096-byte page is actually 4084 bytes. |
| `0x15..0x17`   | `40 20 20`    | Max/min embedded payload fractions and leaf payload fraction (constants in SQLite). |
| `0x18..0x1B`   | `0000 0002`   | File change counter — incremented every time the DB is modified. We made 2 commits (CREATE + INSERT) so it's at 2. |
| `0x1C..0x1F`   | `0000 0002`   | **Database size in pages**, big-endian. `2` — matches `PRAGMA page_count`. |
| `0x2C..0x2F`   | `0000 0004`   | Schema cookie — increments when the schema changes. |
| `0x3A..0x3B`   | `0001`        | Schema format number. |

Two of these I find specifically reassuring:

* The **`0x10` / `1000`** matches `PRAGMA page_size` byte-for-byte.
* The **`0x1C` / `0000 0002`** matches `PRAGMA page_count` byte-for-byte.

`PRAGMA` is literally just reading these bytes off the front of the
file — there's nothing magical about it.

---

# 3. THE SQLITE_SCHEMA PAGE (PAGE 1, AFTER THE 100-BYTE HEADER)

Right after the 100-byte header, on the same physical page, sits the
B-tree page that stores the `sqlite_schema` table.

```text
00000060: 002e 8df8 0d00 0000 010f 7d00 0f7d 0000  ..........}..}..
```

Walking the bytes from offset `0x64` (the page-1 B-tree header):

| Bytes (offset)     | Hex   | Meaning                                          |
|--------------------|-------|--------------------------------------------------|
| `0x64`             | `0d`  | Page type. `0x0d` = **table B-tree leaf page**. |
| `0x65..0x66`       | `0000`| Offset of first freeblock (none — page isn't fragmented). |
| `0x67..0x68`       | `0001`| **Number of cells = 1**. Only one row in sqlite_schema because we have only one user table. |
| `0x69..0x6A`       | `0f7d`| Cell content area starts at offset `0x0F7D` (within the page). |
| `0x6B`             | `00`  | Fragmented free bytes. |
| `0x6C..0x6D`       | `0f7d`| Cell pointer array: one pointer, value `0x0F7D`, points to the single cell. |

The cell at offset `0x0F7D` is what describes our `books` table.

```text
00000f70: 0000 0000 0000 0000 0000 0000 0075 0107  .............u..
00000f80: 1717 1701 8149 7461 626c 6562 6f6f 6b73  .....Itablebooks
00000f90: 626f 6f6b 7302 4352 4541 5445 2054 4142  books.CREATE TAB
00000fa0: 4c45 2062 6f6f 6b73 2028 0a20 2069 6420  LE books (.  id 
00000fb0: 494e 5445 4745 5220 5052 494d 4152 5920  INTEGER PRIMARY
00000fc0: 4b45 592c 0a20 2074 6974 6c65 2054 4558  KEY,.  title TEX
00000fd0: 5420 4e4f 5420 4e55 4c4c 2c0a 2020 6175  T NOT NULL,.  au
00000fe0: 7468 6f72 2054 4558 5420 4e4f 5420 4e55  thor TEXT NOT NU
00000ff0: 4c4c 0a29 0000 0000 0000 0000 0000 0000  LL.)............
```

You can literally read the schema in ASCII on the right — `table books
books ... CREATE TABLE books ( id INTEGER PRIMARY KEY, title TEXT NOT
NULL, author TEXT NOT NULL )`. The five columns of `sqlite_schema`
(`type`, `name`, `tbl_name`, `rootpage`, `sql`) are serialised as a
SQLite record, and SQLite stores its own catalog *inside the database
itself*, on page 1.

Of note: just before the schema cell I can see `81 49` (offset `0x0F86`
in the file). The high-bit-set `81 49` is a SQLite **varint** encoding
of `0xC9 = 201` — but `81 49` (binary `10000001 01001001`) decodes to
`(0x01 << 7) | 0x49 = 201`, which is the byte length of the CREATE TABLE
text as serialised. Varints are why SQLite can hold both tiny and huge
integers without padding.

---

# 4. THE BOOKS TABLE PAGE (PAGE 2, OFFSET 0x1000)

This is the page that physically holds my 5 book rows.

```text
00001000: 0d00 0000 050f 2100 0fcb 0f98 0f63 0f44  ......!......c.D
00001010: 0f21 0000 0000 0000 0000 0000 0000 0000  .!..............
```

Walking the page-2 header (page starts at file offset `0x1000`):

| Bytes (offset in page) | Hex   | Meaning                                              |
|------------------------|-------|------------------------------------------------------|
| `0x00`                 | `0d`  | Page type = table B-tree leaf (same as page 1's schema page). |
| `0x01..0x02`           | `0000`| First freeblock offset = none. |
| `0x03..0x04`           | `0005`| **Cell count = 5**. Matches the 5 rows I inserted. |
| `0x05..0x06`           | `0f21`| Cell content area starts at offset `0x0F21`. |
| `0x07`                 | `00`  | Fragmented free bytes. |
| `0x08..0x11`           | `0fcb 0f98 0f63 0f44 0f21` | **Cell pointer array** — five 2-byte offsets pointing to where each row's cell starts inside this page. |

That cell pointer array is the most important piece. There are exactly
**5 entries**, one per row I inserted. Two more interesting facts:

1. Cell offsets in the array are **sorted by rowid**: pointer 0 is
   the cell for rowid 1, pointer 4 is for rowid 5.
2. But the offsets **decrease** as rowid increases (`0fcb > 0f98 > 0f63
   > 0f44 > 0f21`). That's because SQLite stores cells **from the
   bottom of the page upwards**, while the header + pointer array
   grows from the top down. The empty middle is the page's free
   space — visible as the giant block of `00 00` between offset
   `0x1011` and `0x1F20` in the hex dump.

```
file layout of page 2:

   0x1000  +--------------------+
           |  page header (8B)  |
           +--------------------+
           |  cell ptr array    |
           |  (5 × 2B = 10B)    |
           +--------------------+
           |                    |
           |    free space      |   <-- zeros in the dump
           |   (4071 bytes)     |
           |                    |
           +--------------------+ 0x1F21
           |  row 5 (Database…) |
           +--------------------+ 0x1F44
           |  row 4 (Clean Code)|
           +--------------------+ 0x1F63
           |  row 3 (The C …)   |
           +--------------------+ 0x1F98
           |  row 2 (Designing…)|
           +--------------------+ 0x1FCB
           |  row 1 (Pragmatic…)|
   0x2000  +--------------------+
```

The cell content area is **packed against the end of the page** and
grows backwards. New inserts get placed at lower offsets (the cell
pointer array at the top grows downwards to point at them). This is
the classic **slotted-page** layout — it lets you append cells and
update pointers in two separate regions without rewriting the whole
page.

---

# 5. DECODING ONE ACTUAL ROW

The very first cell (rowid 1, "The Pragmatic Programmer") sits at file
offset `0x1FCB`:

```text
00001fc0: 6e20 4b6c 6570 706d 616e 6e27 0104 003d  n Kleppmann'...=
00001fd0: 2354 6865 2050 7261 676d 6174 6963 2050  #The Pragmatic P
00001fe0: 726f 6772 616d 6d65 7241 6e64 7265 7720  rogrammerAndrew
00001ff0: 4875 6e74 0000 0000 0000 0000 0000 0000  Hunt............
```

| Byte(s)       | Hex      | Meaning                                                                 |
|---------------|----------|-------------------------------------------------------------------------|
| `0x1FCB`      | `27`     | Payload length = **39 bytes** (varint). |
| `0x1FCC`      | `01`     | Rowid = **1** (varint). |
| `0x1FCD`      | `04`     | Record header length = 4 (covers the next 3 serial type bytes). |
| `0x1FCE`      | `00`     | Serial type for `id` column = NULL (because `id` is an alias for the rowid, the value is stored in the cell key, not the body). |
| `0x1FCF`      | `3d`     | Serial type for `title`. For TEXT, type = `2L + 13`, so length = `(0x3d - 13) / 2 = 24`. |
| `0x1FD0`      | `23`     | Serial type for `author`. `(0x23 - 13) / 2 = 11`. |
| `0x1FD1..0x1FE8` | `54 68 65 20 ...` | 24 bytes of title: **"The Pragmatic Programmer"**. |
| `0x1FE9..0x1FF3` | `41 6e 64 72 65 77 20 48 75 6e 74` | 11 bytes of author: **"Andrew Hunt"**. |

Adding it up: `1 (header len) + 3 (serial types) + 0 (NULL id) + 24
(title) + 11 (author) = 39 bytes`, which is exactly the payload length
the cell announced. Both `id` and `title` widths are derivable from the
serial-type byte alone — no separate length field needed, no padding,
no field separators. That's why SQLite databases are so compact.

I did the same decode for rowid 5 (`"Database Internals"` / `"Alex
Petrov"`) at offset `0x1F21` and the numbers matched there too: payload
length `0x21 = 33`, rowid `05`, title serial `0x31 = (49-13)/2 = 18`
characters, author serial `0x23 = 11` characters → `33 = 1 + 3 + 18 +
11`. ✓

---

# 6. COMPARING PRAGMA OUTPUT WITH THE BYTES

| Thing                | What `PRAGMA` said                | What I saw in the bytes                                                    |
|----------------------|-----------------------------------|----------------------------------------------------------------------------|
| Page size            | `4096`                            | `0x10 0x00` at file offset `0x10` (= `0x1000`).                            |
| Page count           | `2`                               | `0x00 0x00 0x00 0x02` at file offset `0x1C`.                               |
| `sqlite_schema.rootpage` for `books` | `2`                  | Page 2 header at `0x1000` starts with `0d` = table leaf B-tree.            |
| Number of `books` rows | 5                               | Cell count field at page-2 offset `0x03..0x04` = `0x00 0x05`.              |
| Encoding             | `UTF-8`                           | Author/title strings are plain ASCII (a subset of UTF-8) visible on the right side of the dump. |

Every single thing `PRAGMA` reports is *literally a field in the file*.

---

# FINAL THINGS I LEARNED

* The `"SQLite format 3"` ASCII string is the entire reason your OS
  knows what kind of file this is.
* The database header is **fixed at 100 bytes**, but the first
  page (which contains the sqlite_schema table) **starts in the same
  page** — so page 1's B-tree header sits at file offset `0x64`, not
  `0x00`. Every other page begins exactly at a `page_size` boundary.
* The first byte of every B-tree page is the **page type byte**:
  * `0x0d` = table leaf
  * `0x05` = table interior
  * `0x0a` = index leaf
  * `0x02` = index interior
  Both pages of my file are `0x0d`, because there's no index yet and
  the table fits entirely on one page.
* SQLite uses a **slotted-page** layout: header + cell pointer array
  grow downward from the top of the page, cell payloads grow upward
  from the bottom, and free space is the gap in the middle. The huge
  block of `00 00`s I saw is literally the free space.
* The catalog is the database. `sqlite_schema` is a regular table
  whose root is page 1, which is exactly why you don't need a separate
  metadata file like `pg_class` on disk.
* **Varints** (variable-length integers, high-bit-as-continuation) are
  used wherever lengths and rowids appear, so very small numbers cost
  1 byte and large numbers grow only when needed.
* **Serial types** encode both "what kind" and "how many bytes" in a
  single varint — e.g. text length is `(serial_type - 13) / 2`. This is
  why SQLite rows don't need per-field length headers like Postgres
  does.
* The first byte of payload data tells you the kind of column, and the
  data follows immediately — no padding, no alignment. That's why my
  5 small rows fit comfortably in 217 bytes at the bottom of page 2
  while leaving 3879 bytes of free space at the top.
