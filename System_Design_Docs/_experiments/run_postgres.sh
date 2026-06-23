#!/bin/bash
# PostgreSQL internals experiment harness.
# Spins up a throwaway cluster, runs a battery of internal-inspection queries,
# and tears it down. All output captured to postgres_experiments.txt.
set -euo pipefail
PGBIN=/opt/homebrew/opt/postgresql@17/bin
HERE="$(cd "$(dirname "$0")" && pwd)"
PGDATA="$HERE/pgdata"
PORT=54399
SOCK="/tmp/adbms_pg_$PORT"
OUT="$HERE/postgres_experiments.txt"
export PGHOST="$SOCK" PGPORT="$PORT" PGDATABASE=lab

psql() { "$PGBIN/psql" -h "$SOCK" -p "$PORT" -d lab "$@"; }

cleanup() {
  "$PGBIN/pg_ctl" -D "$PGDATA" stop -m immediate >/dev/null 2>&1 || true
  rm -rf "$PGDATA" "$SOCK" "$HERE/pg_server.log"
}

# --- clean slate ---
cleanup
mkdir -p "$SOCK"
trap cleanup EXIT

{
echo "############################################################"
echo "# PostgreSQL internals experiment"
"$PGBIN/postgres" --version
echo "############################################################"
} > "$OUT"

# --- init cluster (small shared_buffers so buffer effects are visible) ---
"$PGBIN/initdb" -D "$PGDATA" -U postgres --no-locale -E UTF8 >>"$OUT" 2>&1
cat >> "$PGDATA/postgresql.conf" <<CONF
port = $PORT
unix_socket_directories = '$SOCK'
listen_addresses = ''
shared_buffers = 16MB
fsync = on
wal_level = replica
logging_collector = off
autovacuum = off
CONF

"$PGBIN/pg_ctl" -D "$PGDATA" -l "$HERE/pg_server.log" -w start >>"$OUT" 2>&1
"$PGBIN/createdb" -h "$SOCK" -p "$PORT" -U postgres -O postgres lab
export PGUSER=postgres

# --- schema + data ---
psql -v ON_ERROR_STOP=1 <<'SQL' >>"$OUT" 2>&1
CREATE EXTENSION IF NOT EXISTS pageinspect;
CREATE EXTENSION IF NOT EXISTS pg_buffercache;

CREATE TABLE authors(
  id   int PRIMARY KEY,
  name text NOT NULL
);
CREATE TABLE books(
  id        int PRIMARY KEY,
  author_id int NOT NULL REFERENCES authors(id),
  title     text NOT NULL,
  year      int
);
INSERT INTO authors SELECT g, 'Author '||g FROM generate_series(1,50) g;
INSERT INTO books
  SELECT g, 1+(g % 50), 'Title '||g, 1970+(g % 50)
  FROM generate_series(1,200000) g;
CREATE INDEX idx_books_author ON books(author_id);
CREATE INDEX idx_books_year   ON books(year);
ALTER TABLE books ALTER COLUMN author_id SET STATISTICS 10000;
ALTER TABLE books ALTER COLUMN year SET STATISTICS 10000;
ANALYZE;
SQL

{
echo; echo "================================================================"
echo "EXPERIMENT 1: EXPLAIN (ANALYZE, BUFFERS) on a multi-table join"
echo "================================================================"
} >>"$OUT"
psql -c "EXPLAIN (ANALYZE, BUFFERS, VERBOSE, COSTS)
  SELECT a.name, count(*) AS n, avg(b.year)::numeric(6,1) AS avg_year
  FROM authors a JOIN books b ON b.author_id = a.id
  WHERE b.year > 2010
  GROUP BY a.name
  ORDER BY n DESC
  LIMIT 5;" >>"$OUT" 2>&1

{
echo; echo "================================================================"
echo "EXPERIMENT 2: planner statistics (pg_stats / pg_statistic source)"
echo "================================================================"
} >>"$OUT"
psql -c "SELECT attname, n_distinct, null_frac, avg_width, correlation
         FROM pg_stats WHERE tablename='books' ORDER BY attname;" >>"$OUT" 2>&1
psql -c "SELECT relname, reltuples::bigint AS est_rows, relpages
         FROM pg_class WHERE relname IN ('books','authors','idx_books_author');" >>"$OUT" 2>&1

{
echo; echo "================================================================"
echo "EXPERIMENT 3: MVCC — xmin/xmax/ctid across an UPDATE"
echo "================================================================"
} >>"$OUT"
psql -c "SELECT ctid, xmin, xmax, id, title FROM books WHERE id=123;" >>"$OUT" 2>&1
psql -c "UPDATE books SET title='Title 123 (rev2)' WHERE id=123;" >>"$OUT" 2>&1
{ echo "-- after UPDATE: a NEW row version exists at a new ctid, new xmin:"; } >>"$OUT"
psql -c "SELECT ctid, xmin, xmax, id, title FROM books WHERE id=123;" >>"$OUT" 2>&1
{ echo "-- the SAME logical row, two physical tuple versions on the heap page (pageinspect):"; } >>"$OUT"
psql -c "SELECT lp, t_ctid, t_xmin, t_xmax, t_infomask::bit(16) AS infomask
         FROM heap_page_items(get_raw_page('books', 0))
         WHERE t_xmin IS NOT NULL LIMIT 6;" >>"$OUT" 2>&1
{ echo "-- dead vs live tuples (one UPDATE created one dead tuple):"; } >>"$OUT"
psql -q -c "ANALYZE books;" >/dev/null 2>&1
psql -c "SELECT n_live_tup, n_dead_tup, n_tup_upd FROM pg_stat_user_tables WHERE relname='books';" >>"$OUT" 2>&1

{
echo; echo "================================================================"
echo "EXPERIMENT 4: VACUUM behaviour and the index-cleanup bypass"
echo "================================================================"
} >>"$OUT"
psql -c "VACUUM (VERBOSE, ANALYZE) books;" >>"$OUT" 2>&1
psql -c "SELECT n_live_tup, n_dead_tup FROM pg_stat_user_tables WHERE relname='books';" >>"$OUT" 2>&1

{
echo; echo "================================================================"
echo "EXPERIMENT 5: B-tree index internals (pageinspect)"
echo "================================================================"
} >>"$OUT"
psql -c "SELECT * FROM bt_metap('idx_books_author');" >>"$OUT" 2>&1
psql -c "SELECT type, live_items, dead_items, avg_item_size, page_size, free_size
         FROM bt_page_stats('idx_books_author', 1);" >>"$OUT" 2>&1

{
echo; echo "================================================================"
echo "EXPERIMENT 6: Buffer manager — what's pinned in shared_buffers"
echo "================================================================"
} >>"$OUT"
psql -c "SELECT c.relname,
                count(*) AS buffers,
                pg_size_pretty(count(*)*8192) AS cached,
                round(100.0*count(*) FILTER (WHERE b.isdirty)/count(*),1) AS pct_dirty
         FROM pg_buffercache b JOIN pg_class c ON b.relfilenode=pg_relation_filenode(c.oid)
         WHERE c.relname IN ('books','authors','idx_books_author','idx_books_year')
         GROUP BY c.relname ORDER BY buffers DESC;" >>"$OUT" 2>&1
psql -c "SELECT usagecount, count(*) AS buffers
         FROM pg_buffercache GROUP BY usagecount ORDER BY usagecount;" >>"$OUT" 2>&1

{
echo; echo "================================================================"
echo "EXPERIMENT 7: WAL — LSN advances on writes; checkpoint"
echo "================================================================"
} >>"$OUT"
psql -c "SELECT pg_current_wal_lsn() AS lsn_before;" >>"$OUT" 2>&1
psql -c "UPDATE books SET year=year WHERE id BETWEEN 1 AND 1000;" >>"$OUT" 2>&1
psql -c "SELECT pg_current_wal_lsn() AS lsn_after,
                pg_walfile_name(pg_current_wal_lsn()) AS wal_segment;" >>"$OUT" 2>&1
psql -c "SELECT pg_size_pretty(pg_wal_lsn_diff(pg_current_wal_lsn(),'0/0')) AS total_wal_written;" >>"$OUT" 2>&1
psql -c "CHECKPOINT;" >>"$OUT" 2>&1

{
echo; echo "================================================================"
echo "EXPERIMENT 8: Index vs Seq scan decision flips with selectivity"
echo "================================================================"
} >>"$OUT"
psql -c "EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM books WHERE id = 4242;" >>"$OUT" 2>&1
psql -c "EXPLAIN (ANALYZE, BUFFERS) SELECT count(*) FROM books WHERE year > 1975;" >>"$OUT" 2>&1

# --- teardown ---
"$PGBIN/pg_ctl" -D "$PGDATA" stop -m fast >>"$OUT" 2>&1
trap - EXIT
rm -rf "$PGDATA" "$SOCK" "$HERE/pg_server.log"
echo "DONE. $(wc -l < "$OUT") lines -> $OUT"
