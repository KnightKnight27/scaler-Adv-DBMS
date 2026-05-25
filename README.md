# Lab 4 - SQLite Page Layout Walkthrough

## Student Details
- Name: Anushka
- Roll Number: 10193

## Objective
The objective of this lab is to study how SQLite stores relational data physically on disk using fixed-size pages. The database was created with a page size of 1024 bytes and analyzed using SQLite metadata commands and a hex dump.

## Files Included
- campus.sql
- campus.db
- campus.hex
- README.md

## Database Creation

The database was created using the following command:

sqlite3 campus.db ".read campus.sql"

The database contains three tables:

1. courses  
Stores course details such as:
- Course ID
- Course Code
- Title
- Credits

2. students  
Stores:
- Student ID
- Roll Number
- Name
- Branch
- Semester
- CGPA

3. enrollments  
Stores:
- Student ID
- Course ID
- Grade
- Attendance

## Indexes Created

Indexes were created on:
- students.roll_no
- enrollments.student_id

These indexes improve query performance.

## Hex Dump Generation

Command used:

xxd -g 1 -c 16 campus.db > campus.hex

This converts the database into hexadecimal format for low-level page inspection.

## Database Metadata

Commands used:

PRAGMA page_size;
PRAGMA page_count;

Observed Output:

page_size = 1024 bytes  
page_count = 23 pages

## Database Size Calculation

Total database size:

1024 × 23 = 23552 bytes

This confirms that SQLite stores the database as fixed-size pages.

## SQLite Header Observation

The first bytes of the database file are:

53 51 4C 69 74 65 20 66 6F 72 6D 61 74 20 33 00

This translates to:

SQLite format 3

This confirms the file is a valid SQLite database.

## B-Tree Page Structure

SQLite stores records using B-tree pages.

Common page markers:

- 0x05 → Interior Table Page
- 0x0D → Leaf Table Page

These page types were observed in the hex dump.

## Key Observations

- SQLite divides storage into fixed 1024-byte pages
- Data is stored in B-tree format
- Separate index pages are created
- Leaf pages store actual row data
- Interior pages store references to child pages
- Hex dump reveals low-level database organization

## Learning Outcome

Through this lab, I learned:

- How SQLite organizes relational data internally
- How page-based database storage works
- How indexing improves data access
- How B-tree structures are implemented
- How to inspect raw database pages using hex dumps