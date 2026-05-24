**Role Number:** 24BCS10267  
**Name:** Ujjawal Prabhat

# SQLite3 Hex Dump Analysis: Stock Market Database

## Overview
This repository contains a SQLite3 database (`market_data.db`) storing stock market information and its corresponding hex dump (`stocks_hexdump.txt`) generated using the `xxd` utility.

## Attached Files
* `stocks_hexdump.txt`: The real hex dump of the database.

## 1. Database File Header & Navigation
The database file begins with the standard 100-byte SQLite header located on Page 1.
* **Magic String:** At offset `0x00000000`, we observe the hex `5351 4c69 7465 2066 6f72 6d61 7420 3300`, translating to `SQLite format 3.`.
* **Page Size:** At offset `0x00000010`, the hex value `1000` indicates a page size of 4096 bytes.

## 2. Navigating to the Data B-Tree Node 
Because Page 1 is occupied by the database header and the `sqlite_master` schema table (visible at offset `0x00000f50`), our inserted data resides on Page 2.
* Page 2 begins exactly at byte 4096, which is hex offset `0x00001000`.
* At offset `0x00001000`, we find the B-Tree Page Header flag: `0x0d`. 
* This flag indicates a **Leaf Table B-Tree page**, meaning it contains our actual stock payloads.

## 3. B-Tree Pointers and Structure
Immediately following the page 2 header is the Cell Pointer Array.
* **Cell Count:** Bytes 3-4 of the page header (offset `0x00001003`) show `00 08`, confirming exactly 8 stock records exist on this page.
* **Cell Pointers:** The pointer array begins at offset `0x00001008` with the values `0fce 0fa8 0f7a 0f49 0f1c 0ef1 0ec9 0e9c`. 
* These are byte offsets relative to the start of Page 2 (`0x1000`). For example, the first pointer `0fce` tells us the first row ('AAPL') is located at absolute offset `0x1fce`. The last pointer `0e9c` points to the 8th row ('INTC') at absolute offset `0x1e9c`.

## 4. Record Structure and Payload Analysis
By jumping to absolute offset `0x1e9c` (line `00001e90` in the hex dump), we can analyze the exact byte structure of the 'INTC' row:
* **Payload Header:** The record begins with `2b` (indicating a 43-byte payload size) followed by `08` (Row ID 8).
* **Variable Length Strings:** SQLite uses serial types in the record header to define data. We see type `15` (String length 4 for 'INTC') and type `2f` (String length 17 for 'Intel Corporation'). The ASCII representation confirms these variable lengths clearly on the right side of the dump.
* **IEEE 754 Floats:** The numeric values are not stored as ASCII text. The serial type `07` is used twice in the header, indicating two 8-byte IEEE floating-point numbers. The current price (31.44) and market cap (135.70) are stored as the raw hex blocks `403f 70a3 d70a 3d71` and `4060 f666 6666 6626`.
* **Integer Optimization:** The final serial type in the INTC header is `08`. In SQLite, `08` means "Integer 0" (representing `is_profitable = 0`). Because the value is encoded directly into the serial type, it takes up zero additional bytes in the data payload section. Conversely, the AAPL row at offset `0x1fce` ends its header with type `09`, which means "Integer 1".

## 5. Lookup Process (Example: Finding INTC)
1. SQLite reads the root page (Page 1) and its schema to find the root page for the `stocks` table (Page 2).
2. It jumps to offset `0x1000` (Page 2) and checks the B-Tree Page Header, identifying it as a leaf node (`0x0d`).
3. To find Row ID 8, it counts to the 8th pointer in the Cell Pointer Array (`0e9c`).
4. It adds `0e9c` to the page start `0x1000` to get the absolute address `0x1e9c`.
5. It reads the record header at `0x1e9c`, decodes the serial types (String, String, Float, Float, Int 0), and parses out the Intel stock data.
