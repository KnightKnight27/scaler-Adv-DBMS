# ANALYSING SQLITE3 Database File Hex Dump with xxd Command Line Tool 

## Database name: my_database ; 
## Table name: users ; 

## Contents of the table users on "SELECT * FROM users" Is:- 
id|name 
1|Alice
2|Bob
3|Robin
4|Monica
5|Tushar
6|Samrat


## Command used in the terminal to get the hex dump:- 

### xxd -g 1 -a my_database 

## Hex Dump Output is:- 

00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 04 00 00 00 02  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
00000040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04  ................
00000060: 00 2e 76 89 0d 00 00 00 01 0f b2 00 0f b2 00 00  ..v.............
00000070: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
*
00000fb0: 00 00 4c 01 06 17 17 17 01 79 74 61 62 6c 65 75  ..L......ytableu
00000fc0: 73 65 72 73 75 73 65 72 73 02 43 52 45 41 54 45  sersusers.CREATE
00000fd0: 20 54 41 42 4c 45 20 75 73 65 72 73 28 69 64 20   TABLE users(id 
00000fe0: 49 4e 54 45 47 45 52 20 50 52 49 4d 41 52 59 20  INTEGER PRIMARY 
00000ff0: 4b 45 59 2c 20 6e 61 6d 65 20 54 45 58 54 20 29  KEY, name TEXT )
00001000: 0d 00 00 00 06 0f c3 00 0f f6 0f ee 0f e4 0f d9  ................
00001010: 0f ce 0f c3 00 00 00 00 00 00 00 00 00 00 00 00  ................
00001020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
*
00001fc0: 00 00 00 09 06 03 00 19 53 61 6d 72 61 74 09 05  ........Samrat..
00001fd0: 03 00 19 54 75 73 68 61 72 09 04 03 00 19 4d 6f  ...Tushar.....Mo
00001fe0: 6e 69 63 61 08 03 03 00 17 52 6f 62 69 6e 06 02  nica.....Robin..
00001ff0: 03 00 13 42 6f 62 08 01 03 00 17 41 6c 69 63 65  ...Bob.....Alice 

## SQLite3 Hex Dump Architecture and Navigation Manual 

This document provides a comprehensive, structural breakdown of a SQLite3 database file using a raw hex dump obtained via xxd. It walks through the data layouts of Page 1 (The Header and Database Schema) and Page 2 (The Data B-Tree Node), translating specific hex offsets into physical internal database mechanisms. 

### 🗺️ High-Level Page Mapping 

Based on the provided hex dump, the database uses a standard 4096-byte (4KB) page size. The file contains exactly two pages: 
Page,File Address Range (Hex),Node Type,Content
Page 1,0x00000000 - 0x00000FFF,Table Leaf Node,SQLite Database Header + sqlite_schema system table
Page 2,0x00001000 - 0x00001FFF,Table Leaf Node,User Data (users table records) 

### 📄 Page 1 Breakdown: Database Header & Schema
Page 1 is unique because the first 100 bytes are reserved for the SQLite Database Header. The B-Tree page header immediately follows it at offset 0x64.

1. The 100-Byte Database Header
Looking at the first few rows of your hex dump:

00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 04 00 00 00 02  .....@  ........
53 51 4C 69 74 65 20 66 6F 72 6D 61 74 20 33 00 (Offsets 0–15): The magic string header text: "SQLite format 3\0".

10 00 (Offsets 16–17): Page Size. 0x1000 in hex converts to 4096 bytes in decimal.

40 (Offset 21): Maximum embedded payload fraction (64).

20 (Offset 22): Minimum embedded payload fraction (32).

00 00 00 02 (Offsets 28–31): Size of the database file in pages (2 pages total).

2. The B-Tree Page Header (Page 1)
Because Page 1 contains both the database header and a B-Tree node, the B-Tree page header starts at offset 0x00000060 (specifically at index 0x64):

00000060: 00 2e 76 89 0d 00 00 00 01 0f b2 00 0f b2 00 00  ..v.............
                      ^^ B-Tree Page Header Starts Here
0d (Offset 0x64): Page Type flag. 0x0D indicates a Table Leaf Node (contains keys and data payloads).

00 00 (Offsets 0x65–0x66): Start of the first free block (0 = none).

00 01 (Offsets 0x67–0x68): Number of cells on this page. There is exactly 1 cell (defining the schema of the users table).

0F B2 (Offsets 0x69–0x6A): Start of the cell content area. Content allocation grows backward from the bottom of the page (0x0FB2).

00 (Offset 0x6B): Number of fragmented free bytes.

3. The Cell Pointer Array (Page 1)
Directly following the B-Tree header at offset 0x6C is the Cell Pointer Array:

0F B2 (Offsets 0x6C–0x6D): Points to the exact byte where the 1st cell begins.

4. Schema Cell Payload Analysis
Navigating down to offset 0x00000FB0, we locate the cell structure parsing the schema details:

00000fb0: 00 00 4c 01 06 17 17 17 01 79 74 61 62 6c 65 75  ..L......ytableu
00000fc0: 73 65 72 73 75 73 65 72 73 02 43 52 45 41 54 45  sersusers.CREATE
00000fd0: 20 54 41 42 4c 45 20 75 73 65 72 73 28 69 64 20   TABLE users(id 
...
4C: Varint length of payload data.

01: Varint Row ID (1).

Record Header: Specifies column types using SQLite serial types.

Payload Values: * table: Type of object (0x74 61 62 6c 65).

users: Name of table (0x75 73 65 72 73).

users: Associated table name.

02: Rootpage pointer. This tells SQLite that the structural data for the table users lives inside Page 2.

CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT): The SQL generation statement. 

🌲 Page 2 Breakdown: The Data B-Tree Node
Page 2 begins at offset 0x00001000. It is a pure B-Tree data leaf node, meaning its header begins immediately at byte 0 of the page.

1. B-Tree Page Header (Page 2) 
00001000: 0d 00 00 00 06 0f c3 00 0f f6 0f ee 0f e4 0f d9  ................
          ^^
0d (0x1000): Page Type (Table Leaf Node).

00 00 (0x1001–0x1002): Zero free blocks.

00 06 (0x1003–0x1004): There are 6 records (cells) stored on this page.

0F C3 (0x1005–0x1006): The first active record start pointer byte inside the block (0x0FC3).

00 (0x1007): Zero fragmented free bytes.

2. Cell Pointer Array (Page 2)
Directly following the header at 0x1008 are 6 two-byte integers, mapping out the execution addresses for individual records: 
Index,Hex Value,Target File Address
Cell 0,0F F6,0x1000 + 0x0FF6 = 0x00001FF6
Cell 1,0F EE,0x1000 + 0x0FEE = 0x00001FEE
Cell 2,0F E4,0x1000 + 0x0FE4 = 0x00001FE4
Cell 3,0F D9,0x1000 + 0x0FD9 = 0x00001FD9
Cell 4,0F CE,0x1000 + 0x0FCE = 0x00001FCE
Cell 5,0F C3,0x1000 + 0x0FC3 = 0x00001FC3 

💡 Notice the Sorting Order: SQLite writes content cells from the bottom up (0x1FF6 down to 0x1FC3), meaning Cell 0 points to the last address space visually, while Cell 5 points to the earliest cell space.

### 🔍 Payload Extraction & Record Lookup 
Let's unpack the raw records located between 0x00001FC0 and 0x00001FFF: 

00001fc0: 00 00 00 09 06 03 00 19 53 61 6d 72 61 74 09 05  ........Samrat..
00001fd0: 03 00 19 54 75 73 68 61 72 09 04 03 00 19 4d 6f  ...Tushar.....Mo
00001fe0: 6e 69 63 61 08 03 03 00 17 52 6f 62 69 6e 06 02  nica.....Robin..
00001ff0: 03 00 13 42 6f 62 08 01 03 00 17 41 6c 69 63 65  ...Bob.....Alice 

Parsing Anatomy of an Individual Cell 
Every table leaf cell follows this standard layout structure: 
Payload Size (Varint) 
RowID / Key (Varint) 
Record Header Size (Varint) 
Serial Type Codes (Array of Varints) 
Raw Values Data 
Let's dissect Cell 0 (Target Address: 0x00001FF6) and Cell 5 (Target Address: 0x00001FC7): 
🔸 Breakdown of Cell 0 (Alice) 
Starting at address 0x00001FF6: 08 01 03 00 17 41 6c 69 63 65 
08: Payload size is 8 bytes. 
01: Row ID / Key = 1. 
03: Record header size is 3 bytes (03 00 17). 
00: Serial type for Column 1 (id). 0 implies a NULL value, but because id is defined as an INTEGER PRIMARY KEY, SQLite automatically aliases this to use the Row ID value directly. 
17: Serial type for Column 2 (name). Odd values $N \ge 13$ represent text blobs of length $(N - 13) / 2$. Here, $(23 - 13) / 2 = 5$ bytes. 
41 6C 69 63 65: The 5-byte string data $\rightarrow$ "Alice". 
🔸 Breakdown of Cell 5 (Samrat) 
Starting at address 0x00001FC7: 09 06 03 00 19 53 61 6d 72 61 74 
09: Payload size is 9 bytes. 
06: Row ID / Key = 6.03: Record header size is 3 bytes. 
00: Serial type for Column 1 (id), aliased to Row ID. 
19: Serial type for Column 2 (name). Text length value: $(25 - 13) / 2 = 6$ bytes. 
53 61 6d 72 61 74: The 6-byte string data $\rightarrow$ "Samrat". 

### 📊 Reconstructed Database Table
By navigating through all 6 cell pointers sequentially, we completely extract the underlying user dataset: 

Pointer Address,Row ID (Key),Column: id,Column: name
0x1FF6,1,1,Alice
0x1FEE,2,2,Bob
0x1FE4,3,3,Robin
0x1FD9,4,4,Monica
0x1FCE,5,5,Tushar
0x1FC3,6,6,Samrat 