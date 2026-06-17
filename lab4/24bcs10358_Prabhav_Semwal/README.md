# Lab 4 - SQLite On-Disk Format Analysis using `xxd`

**Author:** Prabhav Semwal
**Database:** `dragonball.db`
**Tools Used:** SQLite3, `xxd`

This experiment explores how SQLite stores data internally using pages and B-tree structures. A custom Dragon Ball themed database was created, dumped using `xxd`, and analyzed through raw hexadecimal offsets.

The database contains:

* One table: `fighters`
* One index: `idx_fighters_power`

The goal of this lab was to understand:

* SQLite database headers
* Page structure
* Table and index B-trees
* Cell pointers
* Indexed lookups using raw hex data

---

## Database Schema

```sql
CREATE TABLE fighters (
    id INTEGER PRIMARY KEY,
    name TEXT,
    race TEXT,
    power_level INTEGER
);

CREATE INDEX idx_fighters_power
ON fighters(power_level);
```

Example rows inserted:

| id | name    | race     | power_level |
| -- | ------- | -------- | ----------- |
| 1  | Goku    | Saiyan   | 9001        |
| 2  | Vegeta  | Saiyan   | 8500        |
| 3  | Piccolo | Namekian | 7000        |
| 4  | Frieza  | Alien    | 12000       |
| 5  | Gohan   | Saiyan   | 8000        |

---

## Commands Used

Create database:

```bash
sqlite3 dragonball.db < create_dragonball.sql
```

Generate hex dump:

```bash
xxd -g 1 -c 16 dragonball.db > dragonball.hex
```

View metadata:

```bash
sqlite3 dragonball.db ".dbinfo"
```

View schema:

```bash
sqlite3 dragonball.db "SELECT type,name,tbl_name,rootpage,sql FROM sqlite_schema;"
```

---

## Database Information

* Page size: 4096 bytes
* Total pages: 3
* Number of tables: 1
* Number of indexes: 1
* Encoding: UTF-8

Page mapping:

| Page | Offset   | Contents                        |
| ---- | -------- | ------------------------------- |
| 1    | `0x0000` | Database header + sqlite_schema |
| 2    | `0x1000` | fighters table B-tree           |
| 3    | `0x2000` | idx_fighters_power index B-tree |

Page offset calculation:

offset=(N-1)*4096

---

## Page 1 - Database Header

The database starts with the SQLite magic string:

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

which represents:

```text
SQLite format 3
```

Page 1 also contains the `sqlite_schema` table which stores metadata about tables and indexes.

---

## Page 2 - fighters Table B-tree

Page 2 begins at offset `0x1000`.

```text
00001000: 0d 00 00 00 05 ...
```

* `0d` → Table leaf page
* `05` → Number of cells (5 fighter rows)

The page contains records such as:

```text
GokuSaiyan
FriezaAlien
PiccoloNamekian
VegetaSaiyan
```

These strings are visible directly inside the raw hex dump.

---

## Page 3 - Index B-tree

Page 3 begins at offset `0x2000`.

```text
00002000: 0a 00 00 00 05 ...
```

* `0a` → Index leaf page
* `05` → Five index entries

The index stores:

* `power_level`
* corresponding rowid

This allows SQLite to search efficiently without scanning the full table.

---

## Query Analysis

Query executed:

```sql
SELECT * FROM fighters WHERE power_level > 9000;
```
(It's over 9000!!)

Returned rows:

| name   | power_level |
| ------ | ----------- |
| Goku   | 9001        |
| Frieza | 12000       |

Query plan:

```text
SEARCH fighters USING INDEX idx_fighters_power (power_level>?)
```

This confirms that SQLite used the B-tree index instead of performing a full table scan.

Lookup process:

1. SQLite checks schema information in page 1.
2. Finds index `idx_fighters_power`.
3. Traverses the index B-tree on page 3.
4. Retrieves matching rowids.
5. Fetches full rows from the fighters table on page 2.

---