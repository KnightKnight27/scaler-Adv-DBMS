#!/bin/bash
# SQLite experiment harness: file format, page layout, query plans, WAL.
# Output -> sqlite_experiments.txt
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DB="$HERE/sqlite_demo.db"
OUT="$HERE/sqlite_experiments.txt"
rm -f "$DB" "$DB-wal" "$DB-shm"

{
echo "############################################################"
echo "# SQLite experiment: file format, page layout, query plans, WAL"
echo "# sqlite3 version:"; sqlite3 --version
echo "############################################################"
} > "$OUT"

sqlite3 "$DB" >>"$OUT" 2>&1 <<'SQL'
PRAGMA page_size = 4096;
PRAGMA journal_mode = DELETE;   -- rollback journal (default)
CREATE TABLE authors(id INTEGER PRIMARY KEY, name TEXT NOT NULL);
CREATE TABLE books(
  id INTEGER PRIMARY KEY,
  author_id INTEGER NOT NULL REFERENCES authors(id),
  title TEXT NOT NULL, year INTEGER);
CREATE INDEX idx_books_author ON books(author_id);
INSERT INTO authors(id,name) VALUES (1,'Stonebraker'),(2,'Codd'),(3,'Gray'),(4,'Lamport');
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n<5000)
INSERT INTO books(id,author_id,title,year)
  SELECT n, (n%4)+1, 'Title '||n, 1970+(n%50) FROM seq;
ANALYZE;
SQL

{
echo; echo "===== PRAGMA page_size / page_count / freelist / cache_size ====="
sqlite3 "$DB" "PRAGMA page_size; PRAGMA page_count; PRAGMA freelist_count; PRAGMA cache_size;"
echo; echo "===== .dbinfo (header + b-tree page accounting) ====="
sqlite3 "$DB" ".dbinfo"
echo; echo "===== schema with rootpage (each btree = one root page) ====="
sqlite3 "$DB" "SELECT type,name,tbl_name,rootpage FROM sqlite_master ORDER BY rootpage;"
echo; echo "===== EXPLAIN QUERY PLAN: join authors x books on indexed FK ====="
sqlite3 "$DB" "EXPLAIN QUERY PLAN SELECT a.name,count(*) FROM authors a JOIN books b ON b.author_id=a.id WHERE b.year>2000 GROUP BY a.name;"
echo; echo "===== EXPLAIN QUERY PLAN: lookup by PRIMARY KEY (rowid) ====="
sqlite3 "$DB" "EXPLAIN QUERY PLAN SELECT * FROM books WHERE id=4242;"
echo; echo "===== EXPLAIN QUERY PLAN: predicate WITHOUT useful index (year) ====="
sqlite3 "$DB" "EXPLAIN QUERY PLAN SELECT * FROM books WHERE year=1999;"
echo; echo "===== EXPLAIN (bytecode VDBE) for the rowid lookup ====="
sqlite3 "$DB" "EXPLAIN SELECT * FROM books WHERE id=4242;"
echo; echo "===== Switching to WAL mode ====="
sqlite3 "$DB" "PRAGMA journal_mode=WAL; PRAGMA wal_autocheckpoint;"
echo; echo "===== Files present after a WAL write (note -wal and -shm) ====="
sqlite3 "$DB" "INSERT INTO authors(id,name) VALUES (99,'WAL writer');"
(cd "$HERE" && ls -la "$(basename "$DB")"*)
echo; echo "===== sqlite_stat1 (ANALYZE output the planner uses) ====="
sqlite3 "$DB" "SELECT tbl,idx,stat FROM sqlite_stat1;"
} >> "$OUT" 2>&1

rm -f "$DB" "$DB-wal" "$DB-shm"
echo "DONE. $(wc -l < "$OUT") lines -> $OUT"
