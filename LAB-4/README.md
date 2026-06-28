# LAB 4 Report : SQLite3 HEX dump analysis

### Creating Database

```cmd
sqlite3 plant.db
```

Now creating table and inserting data :

```SQL
-- Create the plants table
CREATE TABLE plants (
    id INTEGER PRIMARY KEY,
    Name TEXT NOT NULL,
    Color TEXT
);

-- Insert 10 plants into the table
INSERT INTO plants (id, Name, Color) VALUES
(1, 'Rose', 'Red'),
(2, 'Sunflower', 'Yellow'),
(3, 'Tulip', 'Pink'),
(4, 'Lavender', 'Purple'),
(5, 'Daisy', 'White'),
(6, 'Hydrangea', 'Blue'),
(7, 'Marigold', 'Orange'),
(8, 'Orchid', 'Magenta'),
(9, 'Hibiscus', 'Red'),
(10, 'Daffodil', 'Yellow');
```

> The page_size of sqlite3 by default is 4KB (4096 Bytes)

Now using `xxd` to view the hex dump of the plant.db file

- **NOTE** : using `xxd -a -g 1 -s 0 -l 4096 plant.db` to see the first page while filtering out (\*) null bytes grouped per byte.

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 02  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
00000040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02  ................
00000060: 00 2e 95 c9 0d 00 00 00 01 0f 8b 00 0f 8b 00 00  ................
00000070: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
*
00000f80: 00 00 00 00 00 00 00 00 00 00 00 73 01 07 17 19  ...........s....
00000f90: 19 01 81 41 74 61 62 6c 65 70 6c 61 6e 74 73 70  ...Atableplantsp
00000fa0: 6c 61 6e 74 73 02 43 52 45 41 54 45 20 54 41 42  lants.CREATE TAB
00000fb0: 4c 45 20 70 6c 61 6e 74 73 20 28 0a 20 20 20 20  LE plants (.
00000fc0: 69 64 20 49 4e 54 45 47 45 52 20 50 52 49 4d 41  id INTEGER PRIMA
00000fd0: 52 59 20 4b 45 59 2c 0a 20 20 20 20 4e 61 6d 65  RY KEY,.    Name
00000fe0: 20 54 45 58 54 20 4e 4f 54 20 4e 55 4c 4c 2c 0a   TEXT NOT NULL,.
00000ff0: 20 20 20 20 43 6f 6c 6f 72 20 54 45 58 54 0a 29      Color TEXT.)
```

### Analysis of Page-1

We can see that there is no data on page 1, there is just a schema and metadata for the database.

> Every SQLite database begins with a 100-byte header on Page 1.

- `00000000 to 0000000F: 5351 4c69 7465 2066 6f72 6d61 7420 3300`
  - The Magic Header String: "SQLite format 3\0".

- `00000010 to 00000011: 1000`
  - Page Size: 0x1000 in hex equals 4096 bytes. This tells us how to calculate the absolute address of any subsequent page.

- `0000001C to 0000001F: 0000 0002`
  - Size of the database in pages: 2 pages (Page 1 is the schema, Page 2 holds our inserted data).

---

- **NOTE** : using `xxd -a -g 1 -s 4096 -l 4096 plant.db` to see the second page while filtering out (\*) null bytes grouped per byte.

```
00001000: 0d 00 00 00 0a 0f 4c 00 0f f3 0f de 0f cf 0f bb  ......L.........
00001010: 0f ab 0f 98 0f 84 0f 71 0f 60 0f 4c 00 00 00 00  .......q.`.L....
00001020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
*
00001f40: 00 00 00 00 00 00 00 00 00 00 00 00 12 0a 04 00  ................
00001f50: 1d 19 44 61 66 66 6f 64 69 6c 59 65 6c 6c 6f 77  ..DaffodilYellow
00001f60: 0f 09 04 00 1d 13 48 69 62 69 73 63 75 73 52 65  ......HibiscusRe
00001f70: 64 11 08 04 00 19 1b 4f 72 63 68 69 64 4d 61 67  d......OrchidMag
00001f80: 65 6e 74 61 12 07 04 00 1d 19 4d 61 72 69 67 6f  enta......Marigo
00001f90: 6c 64 4f 72 61 6e 67 65 11 06 04 00 1f 15 48 79  ldOrange......Hy
00001fa0: 64 72 61 6e 67 65 61 42 6c 75 65 0e 05 04 00 17  drangeaBlue.....
00001fb0: 17 44 61 69 73 79 57 68 69 74 65 12 04 04 00 1d  .DaisyWhite.....
00001fc0: 19 4c 61 76 65 6e 64 65 72 50 75 72 70 6c 65 0d  .LavenderPurple.
00001fd0: 03 04 00 17 15 54 75 6c 69 70 50 69 6e 6b 13 02  .....TulipPink..
00001fe0: 04 00 1f 19 53 75 6e 66 6c 6f 77 65 72 59 65 6c  ....SunflowerYel
00001ff0: 6c 6f 77 0b 01 04 00 15 13 52 6f 73 65 52 65 64  low......RoseRed
```

### Analysis of Page-2

Right at the beginning of this page, SQLite creates a directory of pointers called the **Cell Pointer Array**.

- Reading line 00001000 from left to right:
  - 0d: This tells SQLite "This is a Leaf B-Tree Page" (meaning it holds actual data, not more branches).

  - 00 00: First freeblock pointer (0 means none).

  - 00 0a: Number of cells. Hex 0a is 10. This page holds exactly 10 rows of plants.

  - 0f 4c: This is where the actual cell data starts.

  - 00: Fragmented free bytes.

> Immediately after that 00, starting at offset 00001008, the pointers begin.

##### Because there are 10 rows on this page, there are 10 pointers immediately following the header. Each pointer is exactly 2 bytes long and tells the database the exact offset within this page where a specific row lives

```text
0f f3 : (Points to Row 1 - RoseRed)
0f de : (Points to Row 2 - SunflowerYellow)
0f cf : (Points to Row 3 - TulipPink)
0f bb : (Points to Row 4)
.
.
.
0f 4c (Points to Row 10)
```

Let's look closely at that very last pointer: `0f 4c` :

- This is a 2-byte offset pointer telling the database engine: "To find the 10th row, go to byte `0f4c` on this page."

- Since this page starts at 00001000 in the file, if we add the pointer's value (0f4c), we get the file address 00001f4c.

- If you look down at line `00001f40` in hex dump above, count over to column c... and what do we find?

        12 0a 04 00 1d 19

The pointer 0f 4c points exactly to the start of the Daffodil metadata

---

### Cell Lookup and Payload Decoding

Now that we have navigated to the absolute address `00001f4c` using the pointer `0f 4c`, we can decode the raw bytes to see exactly how SQLite stores the record data.

Let's break down the byte sequence found at the start of the 10th row:
`12 0a 04 00 1d 19 44 61 66 66 6f 64 69 6c 59 65 6c 6c 6f 77`

SQLite records use a specific serialization format consisting of a **Payload Size**, a **RowID**, a **Record Header**, and the **Record Data**.

#### 1. The Record Preamble

- **`12` (Payload Size):** The hex value `12` is 18 in decimal. This means 18 bytes of data follow the RowID.
- **`0a` (RowID):** The hex value `0a` is 10 in decimal. This is our Primary Key (`id = 10`).

#### 2. The Record Header

Following the RowID is the record header, which dictates the data types and lengths of the columns in the row.

- **`04` (Header Length):** The header is 4 bytes long (which includes this byte itself). Those 4 bytes are `04 00 1d 19`.
- **`00` (Serial Type for Column 1 - `id`):** `00` means NULL or 0 bytes. Because we defined `id` as `INTEGER PRIMARY KEY`, SQLite treats it as an alias for the RowID. To save disk space, it doesn't store the ID twice, hence `00`.
- **`1d` (Serial Type for Column 2 - `Name`):** `1d` in hex is 29 in decimal. For text fields, SQLite calculates the byte length using the formula `(N - 13) / 2`.
  - Calculation: `(29 - 13) / 2 = 16 / 2 = 8` bytes. The plant name is 8 bytes long.
- **`19` (Serial Type for Column 3 - `Color`):** `19` in hex is 25 in decimal.
  - Calculation: `(25 - 13) / 2 = 12 / 2 = 6` bytes. The color string is 6 bytes long.

#### 3. The Record Data (Payload)

Now that the header has told us exactly how many bytes to read for each column, we can parse the remaining ASCII hex values:

- **Next 8 bytes (`44 61 66 66 6f 64 69 6c`):**
  Translates to 'D', 'a', 'f', 'f', 'o', 'd', 'i', 'l' (**Daffodil**).
- **Next 6 bytes (`59 65 6c 6c 6f 77`):**
  Translates to 'Y', 'e', 'l', 'l', 'o', 'w' (**Yellow**).

---

### Verifying Another Record (Row 1)

We can verify this logic by checking the very first pointer in the Cell Pointer Array: `0f f3`.
Adding this to our page offset (`00001000`), we go to address `00001ff3`.

Looking at the hex dump at `00001ff3`:
`0b 01 04 00 15 13 52 6f 73 65 52 65 64`

Let's decode it:

- **`0b`:** Payload size is 11 bytes.
- **`01`:** RowID is 1.
- **`04`:** Header length is 4 bytes (`04 00 15 13`).
- **`00`:** Column 1 (`id`) is aliased.
- **`15`:** Column 2 (`Name`). Hex `15` is decimal 21. Length = `(21 - 13) / 2 = 4` bytes.
- **`13`:** Column 3 (`Color`). Hex `13` is decimal 19. Length = `(19 - 13) / 2 = 3` bytes.
- **`52 6f 73 65`:** 4 bytes of ASCII = **"Rose"**.
- **`52 65 64`:** 3 bytes of ASCII = **"Red"**.

---
