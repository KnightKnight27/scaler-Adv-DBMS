# Lab 4 - Exploring SQLite Page Storage

## Student Details
- Name: Tanmay Mittal
- Roll Number: 24BCS10491

## Objective
This lab examines how SQLite organizes tables and indexes inside the database file. The work builds a small campus database, forces the page size to 1024 bytes, and documents the page layout using metadata queries and a hex dump.

## Files
- `campus.sql`: SQL commands to create and populate the database.
- `campus.db`: Resulting SQLite database.
- `campus.hex`: Hex dump of the database file.
- `README.md`: Observations and analysis of the storage layout.

## Recreate the Database

```powershell
sqlite3 lab4/campus.db ".read lab4/campus.sql"
```

The script defines three tables:

| Table | Purpose |
| --- | --- |
| `courses` | Catalog with course code, name, and credit value |
| `students` | Student records containing roll number, branch, semester, and CGPA |
| `enrollments` | Relationship between students and courses |

The database also includes indexes on `students.roll_no` and `enrollments.student_id`.

## Database Metadata

Commands executed:

```sql
PRAGMA page_size;
PRAGMA page_count;
SELECT name, type, rootpage
FROM sqlite_master
WHERE type IN ('table', 'index')
ORDER BY rootpage;
```

Observed results:

```text
page_size  = 1024 bytes
page_count = 25 pages
file_size  = 25600 bytes
```

The total size matches the page count:

```text
1024 bytes × 25 pages = 25600 bytes
```

## Root Pages

| Object | Type | Root Page |
| --- | --- | --- |
| `courses` | table | 2 |
| `sqlite_autoindex_courses_1` | index | 3 |
| `students` | table | 4 |
| `sqlite_autoindex_students_1` | index | 5 |
| `enrollments` | table | 6 |
| `idx_students_roll_no` | index | 7 |
| `idx_enrollments_student` | index | 9 |
| `sqlite_stat1` | table | 25 |

## Page-level Inspection

The `dbstat` virtual table reveals which pages belong to each object and their types:

```sql
SELECT pageno, name, pagetype, ncell, payload, unused, mx_payload
FROM dbstat
ORDER BY pageno;
```

Important findings:

| Page | Object | Page Type | Cells | Notes |
| --- | --- | --- | --- | --- |
| 1 | `sqlite_schema` | internal | 1 | File header and schema B-tree root |
| 2 | `courses` | leaf | 5 | Contains course data rows |
| 4 | `students` | internal | 5 | Root page of the students table B-tree |
| 6 | `enrollments` | internal | 1 | Root page of the enrollments table B-tree |
| 9 | `idx_enrollments_student` | leaf | 120 | Index entries stored compactly |
| 11-13 | `students` | leaf | 22 each | Data rows for the students table |
| 18-20 | `students` | leaf | 22, 22, 10 | Remaining student records |
| 23-24 | `enrollments` | leaf | 70, 50 | Enrollment rows split across two pages |
| 25 | `sqlite_stat1` | leaf | 4 | Statistics page created by `ANALYZE` |

## Header and B-tree Details

The database file begins with the SQLite magic string:

```text
53 51 4C 69 74 65 20 66 6F 72 6D 61 74 20 33 00
```

This corresponds to:

```text
SQLite format 3
```

The header also confirms the page size is 1024 bytes:

```text
00 04
```

B-tree pages start with a byte indicating their kind:

| First Byte | Meaning |
| --- | --- |
| `0x05` | Interior table B-tree page |
| `0x0D` | Leaf table B-tree page |

Example page headers:

| Page | First Bytes | Interpretation |
| --- | --- | --- |
| Page 2 | `0D 00 00 00 05 ...` | Leaf table page with 5 cells |
| Page 4 | `05 00 00 00 05 ...` | Interior table page with 5 cells |
| Page 11 | `0D 00 00 00 16 ...` | Leaf table page with 22 cells |

## Conclusions

- SQLite stores records in fixed-length pages; this database uses 1024-byte pages.
- As a table grows, its storage may include an interior B-tree root page plus multiple leaf pages.
- Index data is stored in separate B-tree structures, usually using less space because it contains keys and row references rather than full row content.
- `sqlite_master` reports database objects at the logical level, while `dbstat` exposes the physical page allocation.
- The hex dump confirms the file starts with the SQLite signature and then contains B-tree page headers followed by cell payloads.
