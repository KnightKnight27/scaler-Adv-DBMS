# SQLite3 Internal Storage Analysis using XXD

## Objective

The objective of this lab was to explore the internal storage architecture of SQLite3 databases using hexadecimal analysis tools such as `xxd`.

The assignment focuses on:
- SQLite database file structure
- SQLite page layout
- B-Tree node structure
- Cell pointers
- Record storage
- Address navigation
- Table and Index B-Trees
- Row lookup mechanism

The analysis was performed on Fedora KDE Plasma Desktop using SQLite3 and XXD.

---

# Tools Used

| Tool | Purpose |
|------|----------|
| SQLite3 | Database creation and querying |
| xxd | Hex dump analysis |
| dbstat virtual table | SQLite internal page statistics |

---

# Conclusion

This lab provided a detailed understanding of SQLite internal storage architecture using real hexadecimal dumps and B-Tree analysis.
