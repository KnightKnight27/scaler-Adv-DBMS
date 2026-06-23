# SQLite Database Internal Structure Inspection using `xxd`

## Student Information

* **Name:** Jatin Chulet(24BCS10213)
* **Course:** Storage Systems / Database Systems
* **Assignment:** SQLite Database Internal Structure Inspection
* **Database File:** `jatin_projects.db`

---

## Objective

The objective of this assignment is to analyze the internal structure of a SQLite database file by generating and examining its hexadecimal representation using the `xxd` utility. This exercise helps in understanding how SQLite stores metadata, schemas, pages, and records at the byte level.

---

## Tools Used

* SQLite3 Command Line Interface
* `xxd` Hex Dump Utility
* Windows PowerShell
* Git Bash
* SQLite Database Engine

---

## Database Description

A SQLite database named `jatin_projects.db` was created containing a table called `projects`.

### Table Schema

```sql
CREATE TABLE projects(
    project_id INTEGER PRIMARY KEY,
    project_name TEXT,
    team_lead TEXT,
    technology TEXT,
    status TEXT
);
```

### Sample Records

| Project ID | Project Name     | Team Lead     | Technology | Status      |
| ---------- | ---------------- |---------------| ---------- | ----------- |
| 1          | Storage Engine   | Jatin Chulet  | C++        | Completed   |
| 2          | Image Captioning | Kartik Bhatia | PyTorch    | In Progress |
| 3          | MentorConnect    | Kshitij Singh | React      | Completed   |

---

## Generating the Hex Dump

The hexadecimal dump was generated using:

```bash
xxd -g 1 jatin_projects.db > jatin_dump.txt
```

This command displays each byte individually and stores the output in `jatin_dump.txt`.

---

## SQLite File Header Analysis

The first 16 bytes of the database file are:

```text
53 51 4C 69 74 65 20 66 6F 72 6D 61 74 20 33 00
```

ASCII representation:

```text
SQLite format 3
```

This confirms that the file is a valid SQLite database.

---

## Page Size Information

From the SQLite database header:

```text
10 00
```

The value corresponds to:

```text
0x1000 = 4096 bytes
```

Therefore, the database uses a page size of **4096 bytes**, which is SQLite's default page size.

---

## Schema Storage

The schema definition can be observed inside the database pages where the text:

```sql
CREATE TABLE projects(
    project_id INTEGER PRIMARY KEY,
    project_name TEXT,
    team_lead TEXT,
    technology TEXT,
    status TEXT
)
```

appears within the hexadecimal dump.

SQLite stores schema definitions inside the internal table named `sqlite_master`.

---

## Record Storage Observation

The inserted project records are stored within database pages in a compact binary format.

SQLite uses:

* Variable-length integers (Varints)
* B-Tree pages
* Record headers
* Serialized column values

to efficiently store table data.

---

## Key Observations

1. The database begins with the signature **"SQLite format 3"**.
2. SQLite organizes data into fixed-size pages.
3. Table definitions are stored inside the database file itself.
4. Records are stored in B-Tree structures.
5. Hexadecimal analysis reveals both metadata and user data stored in the database.

---

## Conclusion

This experiment demonstrated how SQLite databases are physically stored on disk. By using `xxd`, the binary structure of the database file was inspected, including the file header, page configuration, schema information, and stored records. The exercise provided insight into the low-level storage mechanisms used by SQLite and how relational data is represented internally.
