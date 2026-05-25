# Lab 4 - SQLite Hex Viewer and B-Tree Analysis

## Objective
The objective of this lab is to inspect the internal structure of a SQLite database file using the `xxd` hex viewer and identify the B-Tree structure used by SQLite.

---

## Files Included

- `campus.sql` → SQL schema and insert queries
- `campus.db` → Generated SQLite database
- `campus.hex` → Hex dump of database generated using xxd

---

## Commands Used

### Create Database

```bash
sqlite3 campus.db
```

### Execute SQL File

```sql
.read campus.sql
```

### Generate Hex Dump

```bash
xxd -g 1 -c 16 campus.db > campus.hex
```

---

## Observations

- SQLite database header is visible at the beginning of the file.
- The database stores information in page-based format.
- SQLite internally uses B-Tree pages for table storage.
- Hex dump shows page metadata, offsets, and stored records.

---

## Conclusion

This lab helped in understanding how SQLite organizes data internally using B-Trees and how database files can be inspected at low level using hex viewers.