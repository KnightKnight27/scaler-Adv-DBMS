# Lab 4

I created a SQLite database file and checked it with xxd.

## What I did

Created a database file named `library.db`.

Added one table:

```sql
CREATE TABLE books(id INTEGER PRIMARY KEY, title TEXT);
```

Added a few rows:

```sql
INSERT INTO books(title) VALUES ('algorithms'), ('database'), ('networks');
```

Checked the file in hex with `xxd`.

Checked the SQLite page size, page count, and schema.

## What I found

- The file starts with `SQLite format 3`, so it is a valid SQLite database.
- The page size is 4096 bytes.
- The page count is 2.
- The table `books` has root page 2.
- Page 2 starts with `0d`, which is a table leaf B-tree page.
- The page header also shows 3 cells, which matches the 3 rows I inserted.

## Hex bytes from the table page

```
00001000: 0d 00 00 00 03 0f cb 00 0f e5 0f d8 0f cb 00 00
```
