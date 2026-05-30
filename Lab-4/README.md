# Lab 4: SQLite Database Hex Dump Analysis

## Objective
The objective of this lab is to generate a hex dump of a sample SQLite database (`students.db`) using the `xxd` command and analyze the internal file structure, specifically focusing on the SQLite file header, page structures, and cell pointers.

## File Hex Dump
The complete hex dump of the database was generated using the following command:
```bash
xxd students.db > hexdump.txt
```
You can view the output in `hexdump.txt`.

## Analysis of the Hex Dump

### 1. SQLite Database Header (Page 1)
The first 100 bytes of the SQLite database file contain the database header. It starts at offset `0x00000000`.

- **Magic Header String**: `5351 4c69 7465 2066 6f72 6d61 7420 3300`
  - This translates to `"SQLite format 3\000"` in ASCII, which is the standard 16-byte magic string that identifies all SQLite 3 database files.
- **Page Size**: Bytes at offset `0x0010` indicate the page size (typically `0x1000` which is 4096 bytes).
- **File Format Write/Read Versions**: Offsets `0x0012` and `0x0013` specify the version of the file format (`0x01` or `0x02` for legacy or WAL formats).

### 2. Page Structure (B-Tree Pages)
SQLite databases are composed of pages of a constant size (e.g., 4096 bytes).
The type of page is identified by a flag byte at the start of each page (except for Page 1, where the flag is at offset `100` i.e. `0x0064`):
- `0x02`: Interior index b-tree page
- `0x05`: Interior table b-tree page
- `0x0a`: Leaf index b-tree page
- `0x0d`: Leaf table b-tree page

Looking at the hex dump near `0x00000064`, we can observe the `0x0d` flag indicating a Leaf table b-tree page, which is typically where the `sqlite_master` table is stored initially.

### 3. Cell Pointers
Following the page header (which varies from 8 to 12 bytes depending on the page type), there is an array of cell pointers. These are 2-byte offsets indicating where the actual payload (cell content) is stored within the page.
The cell content grows from the end of the page towards the beginning, while cell pointers grow from the beginning towards the end.

## Conclusion
By observing the `students.db` via `xxd`, we can verify the well-defined structural layout of an SQLite database, consisting of the magic header, configuration bytes, and B-Tree node structures that effectively manage table and index storage.
