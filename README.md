# SQLite3 Hex Dump and B-Tree Analysis

## Objective

The objective of this lab was to inspect the internal structure of a SQLite3 database using a hexadecimal dump utility (`Format-Hex` / `xxd`) and analyze how SQLite stores records using B-Tree pages.

The assignment includes:
- Creating a real SQLite database
- Inspecting the database file in hexadecimal format
- Locating B-Tree nodes
- Understanding page layout
- Identifying cell pointers and record storage
- Explaining lookup/navigation inside SQLite

---

# Database Creation

The database was created using SQLite3.

## SQL Commands Used

```sql
CREATE TABLE users(
    id INTEGER PRIMARY KEY,
    name TEXT
);

INSERT INTO users(name) VALUES ('harshada');
INSERT INTO users(name) VALUES ('harshita');
INSERT INTO users(name) VALUES ('harsha');