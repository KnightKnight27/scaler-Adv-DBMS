# Lab 4 — SQLite Hex Dump & B-Tree Analysis

Overview
- Examine the internal structure of SQLite database files via hex dump analysis.
- Focus on page layouts, database headers, cell pointer arrays, and B-Tree index organization.

Deliverables
- `analysis.md`: Detailed notes on schema layouts, page allocations, and the structure of table and index leaf cells.
- Example hex-dump snippets and annotated offsets.

How to work
1. Open a sample SQLite file (e.g., `test.db`) and run a hex dump: `xxd -g 1 test.db`.
2. Identify the database header (first 100 bytes), page size, and page type bytes.
3. Record observations in `analysis.md` and annotate page-level structures.

References
- SQLite file format documentation: https://www.sqlite.org/fileformat.html
