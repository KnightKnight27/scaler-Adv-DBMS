# Lab 4 - SQLite Storage and B-Tree Hex Dump Analysis

Student name: Lekhana Dinesh  
Roll number: 24BCS10108

## Objective

The objective of this lab is to inspect an SQLite database file using `xxd` and understand how SQLite stores data using pages and B-tree structures.

## Files included

- `campus.sql`
- `campus.db`
- `campus.hex`
- `README.md`

## Database schema summary

The database contains three tables:

- `departments(department_id, name, building)`
- `students(student_id, roll_no, name, department_id, admission_year, email)`
- `enrollments(enrollment_id, student_id, course, semester, grade)`

Two indexes were also created:

- `idx_students_roll_no`
- `idx_enrollments_student_id`

## Commands used

```bash
sqlite3 campus.db ".read campus.sql"
sqlite3 campus.db "PRAGMA page_size;"
sqlite3 campus.db "PRAGMA page_count;"
sqlite3 campus.db "SELECT type, name, tbl_name, rootpage FROM sqlite_schema ORDER BY rootpage;"
xxd -g 1 -c 16 campus.db > campus.hex
```

## Page size and page count

```text
page_size  = 1024
page_count = 29
```

This means each SQLite page is 1024 bytes and the database contains 29 pages.

## sqlite_schema and rootpage output

| type | name | tbl_name | rootpage |
| --- | --- | --- | ---: |
| table | departments | departments | 2 |
| index | sqlite_autoindex_departments_1 | departments | 3 |
| table | students | students | 4 |
| index | sqlite_autoindex_students_1 | students | 5 |
| table | enrollments | enrollments | 6 |
| index | idx_students_roll_no | students | 7 |
| index | idx_enrollments_student_id | enrollments | 8 |

The `rootpage` value tells where the B-tree for each table or index starts.

## SQLite header observation

The file starts with the SQLite signature:

```text
SQLite format 3
```

The first bytes in hex are:

```text
53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

This confirms that `campus.db` is an SQLite database file. The bytes `04 00` after the header show the page size as 1024 bytes.

## Page offset formula

For pages other than page 1:

```text
offset = (rootpage - 1) * page_size
```

Since the page size is 1024:

```text
offset = (rootpage - 1) * 1024
```

For page 1, the SQLite database header uses the first 100 bytes. So the B-tree page header for page 1 starts at offset `0x64`.

## Root page offsets

| Object | Root page | Hex offset |
| --- | ---: | --- |
| SQLite schema metadata | 1 | `0x0064` for B-tree header |
| `departments` table | 2 | `0x0400` |
| `students` table | 4 | `0x0c00` |
| `idx_students_roll_no` index | 7 | `0x1800` |
| `idx_enrollments_student_id` index | 8 | `0x1c00` |

## B-tree page header explanation

A B-tree page header contains:

- page type byte
- first freeblock offset
- number of cells
- start of cell content area
- fragmented free bytes
- cell pointer array

Common SQLite page type values:

- `0x0D` = table leaf B-tree page
- `0x05` = table interior B-tree page
- `0x0A` = index leaf B-tree page
- `0x02` = index interior B-tree page

## Hex excerpts from generated database

### Database file header

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
00000010: 04 00 01 01 00 40 20 20 00 00 00 02 00 00 00 1d
00000020: 00 00 00 00 00 00 00 00 00 00 00 06 00 00 00 04
```

The first line shows the SQLite signature. The bytes `04 00` represent the page size of 1024 bytes.

### Page 1 B-tree header around offset `0x64`

```text
offset 0x0060:
00 2e 8a 14 05 00 00 00 01 03 fb 00 00 00 00 0a
```

The B-tree header starts at offset `0x64`. The important byte is `0x05`, which means this is a table interior B-tree page.

### `students` table root page at offset `0x0c00`

```text
offset 0x0c00:
05 00 00 00 05 03 e7 00 00 00 00 10 03 fb 03 f6
03 f1 03 ec 03 e7 02 75 02 43 02 14 01 e3 01 af
```

The first byte is `0x05`, so the `students` table root page is also a table interior B-tree page. The page header shows the page type, number of cells, and start of cell content area.

### Index root page at offset `0x1800`

```text
offset 0x1800:
02 00 00 00 02 03 d2 00 00 00 00 13 03 e9 03 d2
03 a2 03 8f 03 7c 03 69 03 56 03 43 03 30 03 1d
```

The first byte is `0x02`, which means this is an index interior B-tree page.

## Conclusion

SQLite stores tables and indexes inside fixed-size pages. The `sqlite_schema` table gives the root page for each table and index. Using `xxd`, we can locate those pages inside the database file and observe B-tree page headers directly from the hex dump.