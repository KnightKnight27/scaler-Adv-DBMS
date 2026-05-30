# Lab 4 - Hexdump of a SQLite file

Goal - To see what actually lands on disk after a few simple SQL statements, 
so I built a tiny `library.db` and dumped it with `xxd`.

## Setup

```sql
CREATE TABLE books(id INTEGER PRIMARY KEY, title TEXT);

INSERT INTO books(title) VALUES ('algorithms'), ('database'), ('networks');
```

Three rows, one table. Nothing fancy.

From the SQLite shell I confirmed the basics:

```text
page_size: 4096
page_count: 2
```

Schema inspection showed `books` with root page `2`. That lines up with a 2-page file: page 1 is always the meta/header area, page 2 is where the table data lives.

## File header

The first bytes spell out `SQLite format 3` — so we're looking at a real DB file, not random binary.

## Page 2 (the `books` table)

Offset `0x1000` is page 2 (4096-byte pages, zero-based indexing in the dump). The line that caught my eye:

```text
00001000: 0d 00 00 00 03 0f cb 00 0f e5 0f d8 0f cb 00 00
```

Breaking that down:

- `0d` — table **leaf** B-tree page (interior pages use `0x05` for tables; I didn't have enough data to see one here).
- The `03` a few bytes in is the cell count. I inserted three rows, so three cells on the leaf page checks out.
- The following offsets (`0f cb`, `0f e5`, `0f d8`, …) are pointers into the page where each cell's payload starts.

So the on-disk layout matches what I'd expect from the inserts: one leaf page holding all three book titles, no overflow pages yet.

## Takeaway

SQLite isn't storing rows as plain text in the file. You get paged B-trees with typed page headers, and even a three-row table already shows up as structured binary once you look past the ASCII header string. Hexdumping plus `.schema` / `PRAGMA` is enough to connect the SQL you ran to specific bytes on a page.
