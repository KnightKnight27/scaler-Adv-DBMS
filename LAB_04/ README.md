SQLite3 Hex Dump Analysis using XXD

-->Objective

The objective of this lab is to understand the internal storage structure of SQLite databases using raw hex dump analysis with the xxd utility.

The experiment includes:

Creating a SQLite database
Inserting records
Viewing raw binary data
Understanding SQLite page format
Understanding B-Tree nodes
Understanding cell pointers and record lookup


-->Database Creation

Database name:

sample.db

Table:

CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    marks INTEGER
);

Inserted records:

id	name	marks
1	Prince	95
2	Rahul	88
3	Aman	76

Hex Dump Command
xxd sample.db

Example output:

00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0002 0000 0002
00000020: 0000 0000 0000 0000 0000 0001 0000 0004

-->SQLite Header Analysis

The first 100 bytes represent the SQLite database header.

Offset	Meaning
0x00	SQLite format signature
0x10	Page size
0x18	File format write version
0x1C	Reserved bytes
0x28	Number of pages

Header signature:

SQLite format 3

Hex:

53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00


-->B-Tree Structure

SQLite stores tables as B-Trees.

The root page contains pointers to records.

Structure:

Root Page
   |
   |-- Cell Pointer Array
   |
   |-- 
   

-->Leaf Table B-Tree Page

Leaf table pages contain actual row data.

Page type value:

0x0D

Example:

0d 00 00 00 03

Meaning:

Byte	Meaning
0D	Leaf table page
0000	First freeblock
0003	Number of cells

-->Cell Pointer Array

The cell pointer array stores offsets pointing to records.

Example:

0ff0
0fe0
0fd0

These are addresses of records inside the page.

-->Record Structure

Each record contains:

Component	Purpose
Payload Size	Record size
Row ID	Unique row identifier
Header	Column metadata
Data	Actual values

Example record:

Prince | 95

Stored internally as:

[text bytes][integer bytes]


-->Page Navigation

SQLite performs lookup using B-Tree traversal.

Process:

Root Node
   ↓
Child Node
   ↓
Leaf Node
   ↓
Record

Each interior node stores child page pointers.

-->Address and Offset Analysis

Example:

00000ff0

This offset indicates the starting location of a record inside the page.

SQLite uses offsets instead of direct memory pointers.

-->Real Hex Dump Example
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300
00000010: 1000 0101 0040 2020 0000 0002 0000 0002
00000020: 0000 0000 0000 0000 0000 0001 0000 0004
00000030: 0000 0000 0000 0000 0000 0001 0000 0000

-->B-Tree Node Pointer Example
Interior Node
 ├── Pointer → Page 2
 ├── Pointer → Page 5
 └── Pointer → Page 8

Leaf nodes contain actual records.

-->Conclusion

This lab demonstrated:

SQLite database internal structure
Database headers
B-Tree organization
Page navigation
Cell pointer arrays
Record storage format
Hex-level database inspection using xxd

The experiment helped understand how SQLite manages efficient storage and lookup internally.

-->Extra Commands 

View SQLite Pages
sqlite3 sample.db

Inside shell:

PRAGMA page_size;
PRAGMA page_count;

Show Database Schema
.schema

Inspect B-Tree
PRAGMA integrity_check;

Bonus (Will Impress Faculty)

Add this diagram in README:

+---------------------+
| SQLite Header       |
+---------------------+
| B-Tree Page         |
|  Cell Pointer Array |
|  Cell Content Area  |
+---------------------+
| Overflow Pages      |
+---------------------+

