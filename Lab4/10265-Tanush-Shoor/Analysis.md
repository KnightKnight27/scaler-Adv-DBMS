# Internal Structure Examination of a SQLite3 Database Using XXD

**Name:** Tanush Shoor  
**Roll Number/ Student ID:** 24BCS10265

---

## Aim

The aim of this experiment was to examine the internal organization of a SQLite3 database file by inspecting hexadecimal dumps produced by the `xxd` tool. Through this analysis, the experiment illustrates the way SQLite manages and stores the following internally:

* Database headers
* B-tree pages
* Table records
* Metadata
* Cell pointer arrays
* Record payloads
* Schema definitions

The entire analysis was carried out on a custom-built `clubs` database.

---

## Database Creation

The database was set up using SQLite3.

### Schema Definition

```sql
CREATE TABLE clubs (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    location TEXT NOT NULL
);
```

---

## Records in the Database

The table holds 5 club entries as shown below:

| ID | Club Name     | Location      |
| -- | ------------- | ------------- |
| 1  | Chess Club    | New York      |
| 2  | Football Club | London        |
| 3  | Book Club     | San Francisco |
| 4  | Music Club    | Los Angeles   |
| 5  | Art Club      | Paris         |

---

## Retrieving Database Metadata

The following SQL commands were executed to gather metadata:

```sql
PRAGMA page_size;
PRAGMA page_count;

SELECT name, rootpage
FROM sqlite_master;
```

### Results

```text
Page Size  : 4096 bytes
Page Count : 2
```

### Root Page Mapping

| Object | Root Page |
| ------ | --------- |
| clubs  | 2         |

---

## Physical Layout of the File

With each page occupying 4096 bytes, the file offsets are as follows:

| Page Number | File Offset |
| ----------- | ----------- |
| Page 1      | 0x0000      |
| Page 2      | 0x1000      |

---

## Inspecting the SQLite File Header

The first 512 bytes of the file were examined using:

```bash
xxd -g 1 -l 512 clubs
```

### Hex Dump Output

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

### Decoded ASCII

```text
SQLite format 3
```

This string is the SQLite magic header, confirming the file is a valid SQLite3 database.

---

## Header Byte Breakdown

| Bytes                | Meaning |
| -------------------- | ------- |
| 53 51 4C 69          | SQLi    |
| 74 65                | te      |
| 20 66 6F 72 6D 61 74 | format  |
| 20 33 00             | 3       |

---

## Page Size Verification

The bytes at the relevant offset:

```text
10 00
```

Translating to hex:

```text
0x1000 = 4096
```

This confirms the result returned by `PRAGMA page_size`.

---

## SQLite B-Tree Organization

SQLite uses B-tree structures to store all data internally. The two pages in this database serve the following roles:

| Page | Type              | Purpose                |
| ---- | ----------------- | ---------------------- |
| 1    | Table B-tree      | sqlite_master metadata |
| 2    | Leaf Table B-tree | clubs table data       |

---

## High-Level Database Structure

```text
clubs
│
├── Page 1 (0x0000 – 0x0FFF)
│     ├── SQLite File Header
│     ├── sqlite_master B-tree
│     ├── Schema Records
│     └── CREATE TABLE statements
│
└── Page 2 (0x1000 – 0x1FFF)
      ├── clubs Table B-tree
      ├── Cell Pointer Array
      └── Club Records
```

---

## Examining Page 2 — The Clubs Table

Page 2 serves as the root page for the `clubs` table. It was inspected using:

```bash
xxd -g 1 -s 4096 -l 512 clubs
```

### First 8 Bytes of Page 2

```text
0d 00 00 00 05 0f 79 00
```

---

## Page Header Format

The leaf table B-tree page header follows this structure:

| Offset | Size    | Meaning               |
| ------ | ------- | --------------------- |
| 0      | 1 byte  | Page Type             |
| 1–2    | 2 bytes | First Freeblock       |
| 3–4    | 2 bytes | Number of Cells       |
| 5–6    | 2 bytes | Start of Cell Content |
| 7      | 1 byte  | Fragmented Free Bytes |

---

## Decoded Page Header Values

| Bytes | Value | Meaning                |
| ----- | ----- | ---------------------- |
| 0d    | 13    | Leaf Table B-tree Page |
| 00 00 | 0     | No freeblocks present  |
| 00 05 | 5     | 5 records stored       |
| 0f 79 | 3961  | Cell content start     |
| 00    | 0     | No fragmented bytes    |

---

## Cell Pointer Array

Located immediately after the page header, the 5 cell pointers are:

```text
0f dc
0f c3
0f a7
0f 8c
0f 79
```

Each pointer holds the offset of a record within the page, allowing direct access to row data.

---

## Cell Pointer Example

Taking the first pointer:

```text
0f dc  →  0x0FDC = 4060
```

Absolute file offset:

```text
4096 + 4060 = 8156
```

This tells SQLite exactly where in the file to find the corresponding row.

---

## Page Memory Layout

SQLite pages are organized so that headers and cell pointers grow downward from the top, while record data grows upward from the bottom:

```text
+----------------------+
| B-tree Page Header   |
+----------------------+
| Cell Pointer Array   |
+----------------------+
| Free Space           |
|                      |
|                      |
+----------------------+
| Record Data          |
| (grows upward)       |
+----------------------+
```

This layout reduces the need for data movement during insertions and deletions.

---

## Examining the Record Payload

Record data begins at offset `0x0F79`. The following command was used to inspect it:

```bash
xxd -g 1 -s 8057 -l 512 clubs
```

---

## Decoded Record Data

### Club Name Field

```text
Hex   : 43 68 65 73 73 20 43 6c 75 62
ASCII : Chess Club
```

### Location Field

```text
Hex   : 4e 65 77 20 59 6f 72 6b
ASCII : New York
```

---

## All Records Found in the Dump

| Decoded Name  | Decoded Location |
| ------------- | ---------------- |
| Football Club | London           |
| Book Club     | San Francisco    |
| Music Club    | Los Angeles      |
| Art Club      | Paris            |

This confirms that SQLite packs multiple rows efficiently within a single leaf B-tree page.

---

## SQLite Record Storage Format

Each record is stored in the following sequence:

```text
[Payload Size]
[Row ID]
[Record Header Size]
[Serial Types]
[Actual Column Data]
```

---

## Inspecting sqlite_master

The `sqlite_master` table holds schema-level metadata for the database. The hex dump reveals the string:

```text
tableclubsclubs
```

This is the metadata entry associated with the `clubs` table.

---

## Schema Stored in the File

Running the `strings` command:

```bash
strings clubs
```

produces the following SQL statement embedded inside the binary file:

```sql
CREATE TABLE clubs (id INTEGER PRIMARY KEY,name TEXT NOT NULL,location TEXT NOT NULL)
```

This confirms that SQLite stores the full schema definition as plain text within the database file itself.

---

## Root Page References

The file also stores root page mappings internally:

| Object | Root Page |
| ------ | --------- |
| clubs  | 2         |

This is consistent with the output of:

```sql
SELECT name, rootpage FROM sqlite_master;
```

---

## Key Observations

1. SQLite relies entirely on B-tree structures for data storage.
2. Database metadata is itself stored as a table (`sqlite_master`).
3. All records are variable in length.
4. Cell pointer arrays enable efficient and direct row lookups.
5. Page headers and record data grow in opposite directions within each page.
6. A single page can hold multiple rows in a compact form.
7. SQL schema definitions are physically embedded within the database file.
8. Binary database files can be directly analyzed using hex tools such as `xxd`.
