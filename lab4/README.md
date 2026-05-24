# SQLite3 Hex Dump and B-Tree Analysis

## Objective

The objective of this lab is to analyze the internal structure of a SQLite3 database using hexadecimal dumps generated with the `xxd` utility. The experiment demonstrates SQLite page structure, B-tree nodes, page headers, cell pointer arrays, and record storage.

---

# Tools Used

* SQLite3
* xxd
* Ubuntu WSL Terminal

---

# Database Creation

A SQLite database named `company.db` was created.

## SQL Commands Used

```sql
CREATE TABLE employees (
    id INTEGER PRIMARY KEY,
    name TEXT,
    salary INTEGER
);

INSERT INTO employees (name, salary)
VALUES
('Alice', 50000),
('Bob', 60000),
('Charlie', 70000),
('David', 80000);
```

---

# Database Verification

The following commands were used to verify the database contents:

```sql
.tables
SELECT * FROM employees;
```

Output:

```text
employees

1|Alice|50000
2|Bob|60000
3|Charlie|70000
4|David|80000
```

---

# SQLite Header Analysis

The first 100 bytes of the database were inspected using:

```bash
xxd -l 100 company.db
```

Important output:

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300
```

This corresponds to:

```text
SQLite format 3
```

which confirms that the file is a valid SQLite3 database.

---

# Page Information

The following commands were executed:

```bash
sqlite3 company.db "PRAGMA page_size;"
sqlite3 company.db "PRAGMA page_count;"
```

Output:

```text
4096
2
```

Interpretation:

* Page size = 4096 bytes
* Total pages = 2

Page 1 contains database schema information.

Page 2 contains table data stored as a B-tree leaf page.

---

# B-Tree Structure Analysis

The second page of the database was extracted using:

```bash
dd if=company.db bs=4096 skip=1 count=1 | xxd
```

Important output:

```text
00000000: 0d00 0000 040f c800 0ff2 0fe6 0fd6 0fc8
```

## Interpretation of B-Tree Header

| Hex Value | Meaning                       |
| --------- | ----------------------------- |
| `0d`      | Leaf table B-tree page        |
| `0004`    | Number of cells (records) = 4 |
| `0fc8`    | Start of cell content area    |

---

# Cell Pointer Array

The values:

```text
0ff2 0fe6 0fd6 0fc8
```

represent pointers to records stored inside the page.

| Pointer | Meaning             |
| ------- | ------------------- |
| `0ff2`  | Address of record 1 |
| `0fe6`  | Address of record 2 |
| `0fd6`  | Address of record 3 |
| `0fc8`  | Address of record 4 |

SQLite uses these pointers to locate rows efficiently inside the B-tree page.

---

# Record Storage

The employee records were visible inside the hexadecimal dump and ASCII strings.

Example strings extracted:

```text
Alice
Bob
Charlie
David
```

This confirms that SQLite stores table records directly inside B-tree leaf pages.

---

# SQLite Page Navigation

SQLite databases are organized into fixed-size pages.

Structure:

| Section            | Description              |
| ------------------ | ------------------------ |
| Database Header    | First 100 bytes          |
| B-tree Pages       | Store tables and indexes |
| Cell Pointer Array | Points to records        |
| Record Payload     | Actual row data          |

Page navigation occurs using page numbers and offsets stored inside B-tree structures.

---

# Observations

* SQLite uses B-tree structures for storing table records.
* The database header begins with the signature `SQLite format 3`.
* Records are stored inside leaf B-tree pages.
* Cell pointer arrays provide offsets to record payloads.
* SQLite efficiently organizes data using fixed-size pages.

---

# Screenshots
![alt text](<screenshots/Screenshot 2026-05-24 230736.png>)


![alt text](<screenshots/Screenshot 2026-05-24 230842.png>) 

![alt text](<screenshots/Screenshot 2026-05-24 230920.png>) 

![alt text](<screenshots/Screenshot 2026-05-24 231126.png>) 

![alt text](<screenshots/Screenshot 2026-05-24 231139.png>) 

![alt text](<screenshots/Screenshot 2026-05-24 231210.png>)

# Conclusion

This experiment successfully demonstrated the low-level internal structure of a SQLite3 database. Using `xxd`, the database header, page structure, B-tree node format, and record pointers were analyzed. The experiment provided practical understanding of how SQLite stores and navigates records internally.

