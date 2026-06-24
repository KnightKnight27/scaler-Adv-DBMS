# SQLite3 Internal Storage Analysis using `xxd`

## Objective
This project examines the internal binary structure of a SQLite3 database file using the `xxd` hex dumping tool. The goal is to manually parse and understand:
- Database Header
- Page Layout and Headers
- Cell Pointer Arrays
- B-Tree Nodes and Record Payloads

## Implementation Overview

We performed a deep dive into the binary structure of `students.db`. 

### 1. Database Header Extraction
We used `xxd` to read the first 100 bytes of the database file. From this, we successfully identified:
- The Magic Header String (`SQLite format 3\000`)
- The Database Page Size (found at offset 16, resulting in `4096` bytes)

### 2. Table Data Location
Using the SQLite master schema, we determined that the `students` table resides on Root Page 2. We calculated its absolute offset as `4096` (`0x1000`).

### 3. Page Layout & Cell Pointers
By dumping Page 2, we analyzed the B-Tree Page Header and identified:
- The page type as a Leaf Table B-Tree (`0x0d`)
- The number of cells/records stored on the page
- The Cell Pointer Array, giving us the exact offsets of the records (e.g., offsets `0x1fb4` and `0x1f67`).

### 4. Payload Decoding
We jumped to the memory offsets indicated by the cell pointers and manually decoded the B-Tree Leaf Node. This involved:
- Decoding the Variable-Length Integers (Varints) representing the Payload Size and RowID.
- Decoding the Record Header to extract the Serial Types (data types and string lengths for each column).
- Parsing the raw hex back into the actual strings and integers stored in the database (e.g., retrieving the records for "Kartik" and "Prashansa").

## Files Created

- `students.db`: The SQLite database analyzed in this assignment.
- `students.hex`: A full hex dump of the database.
- `page2.hex`: A hex dump isolating the beginning of Root Page 2 (containing the page header and cell pointer array).
- `page2_end.hex`: A hex dump isolating the end of Root Page 2 (containing the actual B-Tree records and data payloads).
- A detailed walkthrough artifact outlining the step-by-step memory addresses, hex values, and decoded strings.
