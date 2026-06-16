# Lab 4 — SQLite Hexdump with xxd

### Submitted By
| Detail | Information |
|---|---|
| Name | Rudhar Bajaj |
| Roll Number | 24BCS10143 |

---

I created a SQLite database file and checked it with `xxd`.

## What I did

1. Created a database file named `lab4.db`.
2. Added one table:

```sql
CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT);
```

3. Added a few rows:

```sql
INSERT INTO t(name) VALUES ('alice'), ('bob'), ('carol'), ('dave');
```

4. Checked the file in hex with `xxd`.
5. Checked the SQLite page size, page count, and schema.

## What I found

- The file starts with `SQLite format 3`, so it is a valid SQLite database.
- The page size is `4096` bytes.
- The page count is `2`.
- The table `t` has root page `2`.
- Page `2` starts with `0d`, which is a table leaf B-tree page.
- The page header also shows 4 cells, which matches the 4 rows I inserted.

## Hex bytes from the table page

```text
00001000: 0d 00 00 00 04 0f db 00 0f f6 0f ee 0f e4 0f db
```

## Simple reading of the page

- `0d` means this is a table leaf B-tree page.
- `00 00` means there is no free block.
- `00 04` means there are 4 cells.
- `0f db` is where the cell content starts.
- The next values are the cell pointers.

## Conclusion

SQLite keeps the table data inside the same database file.
The B-tree for the table starts at the root page, and in this file that page is `2`.