#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$DOC_DIR/../.." && pwd)"
WORK_DIR="$ROOT_DIR/.local/postgresql-internals"
PGDATA="$WORK_DIR/pgdata"
PGPORT="${PGPORT:-55433}"
PGSOCKET="${PGSOCKET:-/tmp/adbms-pg-internals-${USER:-local}}"
PGDATABASE="adbms_pg_internals_lab"
OUTPUT="$DOC_DIR/EXPERIMENT_RESULTS.md"

mkdir -p "$WORK_DIR" "$PGSOCKET"

if [ ! -s "$PGDATA/PG_VERSION" ]; then
  initdb -D "$PGDATA" --auth=trust --username=postgres --no-locale >/dev/null
fi

pg_started_by_script=0
if ! pg_ctl -D "$PGDATA" status >/dev/null 2>&1; then
  pg_ctl -D "$PGDATA" -l "$WORK_DIR/postgres.log" -o "-k $PGSOCKET -p $PGPORT" -w start >/dev/null
  pg_started_by_script=1
fi

cleanup() {
  if [ "$pg_started_by_script" -eq 1 ]; then
    pg_ctl -D "$PGDATA" -w stop -m fast >/dev/null
  fi
}
trap cleanup EXIT

dropdb -h "$PGSOCKET" -p "$PGPORT" -U postgres --if-exists "$PGDATABASE" >/dev/null 2>&1 || true
createdb -h "$PGSOCKET" -p "$PGPORT" -U postgres "$PGDATABASE"

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -v ON_ERROR_STOP=1 >/dev/null <<'SQL'
CREATE EXTENSION IF NOT EXISTS pageinspect;

CREATE TABLE accounts (
  account_id integer PRIMARY KEY,
  region text NOT NULL,
  status text NOT NULL,
  balance numeric(12, 2) NOT NULL
);

CREATE TABLE ledger_entries (
  entry_id integer PRIMARY KEY,
  account_id integer NOT NULL REFERENCES accounts(account_id),
  created_at date NOT NULL,
  amount numeric(12, 2) NOT NULL
);

INSERT INTO accounts (account_id, region, status, balance)
SELECT
  g,
  CASE
    WHEN g % 4 = 0 THEN 'north'
    WHEN g % 4 = 1 THEN 'south'
    WHEN g % 4 = 2 THEN 'east'
    ELSE 'west'
  END,
  CASE WHEN g % 9 = 0 THEN 'blocked' ELSE 'active' END,
  ((g * 19) % 200000) / 100.0
FROM generate_series(1, 12000) AS g;

INSERT INTO ledger_entries (entry_id, account_id, created_at, amount)
SELECT
  g,
  ((g * 41) % 12000) + 1,
  DATE '2025-01-01' + ((g * 11) % 540),
  (((g * 23) % 50000) - 25000) / 100.0
FROM generate_series(1, 90000) AS g;

CREATE INDEX idx_accounts_region_status ON accounts (region, status);
CREATE INDEX idx_ledger_account_created ON ledger_entries (account_id, created_at);
CREATE INDEX idx_ledger_created ON ledger_entries (created_at);
ANALYZE;

CREATE TABLE mvcc_demo (
  id integer PRIMARY KEY,
  note text NOT NULL
);

INSERT INTO mvcc_demo VALUES (1, 'first version');
SQL

versions="$WORK_DIR/versions.txt"
settings="$WORK_DIR/settings.txt"
explain="$WORK_DIR/explain.txt"
stats="$WORK_DIR/stats.txt"
btree="$WORK_DIR/btree.txt"
mvcc="$WORK_DIR/mvcc.txt"
wal="$WORK_DIR/wal.txt"

{
  postgres --version
  psql --version
  psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -qAt -c "SELECT current_database();"
} > "$versions"

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -q > "$settings" <<'SQL'
SHOW shared_buffers;
SHOW wal_level;
SHOW checkpoint_timeout;
SHOW max_wal_size;
SHOW default_statistics_target;
SQL

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -qAt > "$explain" <<'SQL'
EXPLAIN (ANALYZE, BUFFERS, TIMING OFF)
SELECT a.region, COUNT(*) AS entry_count, ROUND(SUM(l.amount), 2) AS net_amount
FROM accounts a
JOIN ledger_entries l ON l.account_id = a.account_id
WHERE a.region = 'north'
  AND a.status = 'active'
  AND l.created_at >= DATE '2026-01-01'
GROUP BY a.region;
SQL

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -q > "$stats" <<'SQL'
SELECT tablename, attname, n_distinct, most_common_vals
FROM pg_stats
WHERE schemaname = 'public'
  AND tablename IN ('accounts', 'ledger_entries')
  AND attname IN ('region', 'status', 'created_at')
ORDER BY tablename, attname;
SQL

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -q > "$btree" <<'SQL'
SELECT *
FROM bt_metap('idx_ledger_account_created');
SQL

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -q > "$mvcc" <<'SQL'
SELECT 'before update' AS phase, ctid, xmin, xmax, id, note
FROM mvcc_demo;

UPDATE mvcc_demo
SET note = 'second version'
WHERE id = 1
RETURNING 'after update' AS phase, ctid, xmin, xmax, id, note;

SELECT lp, lp_flags, t_xmin, t_xmax, t_ctid
FROM heap_page_items(get_raw_page('mvcc_demo', 0))
ORDER BY lp;
SQL

psql -h "$PGSOCKET" -p "$PGPORT" -U postgres -d "$PGDATABASE" -X -q > "$wal" <<'SQL'
SELECT pg_current_wal_lsn() AS lsn_before \gset
UPDATE accounts
SET balance = balance + 10
WHERE account_id BETWEEN 100 AND 200;
SELECT pg_current_wal_lsn() AS lsn_after,
       pg_wal_lsn_diff(pg_current_wal_lsn(), :'lsn_before') AS wal_bytes_generated;
SQL

{
  printf "# PostgreSQL Internals Experiment Results\n\n"
  printf "Generated locally by \`experiments/run_experiments.sh\`.\n\n"
  printf "## Tool Versions\n\n"
  printf "\`\`\`text\n"
  cat "$versions"
  printf "\`\`\`\n\n"
  printf "## Server Settings\n\n"
  printf "\`\`\`text\n"
  cat "$settings"
  printf "\`\`\`\n\n"
  printf "## EXPLAIN ANALYZE With Buffers\n\n"
  printf "\`\`\`text\n"
  cat "$explain"
  printf "\`\`\`\n\n"
  printf "## Planner Statistics Sample\n\n"
  printf "\`\`\`text\n"
  cat "$stats"
  printf "\`\`\`\n\n"
  printf "## B-Tree Metadata\n\n"
  printf "\`\`\`text\n"
  cat "$btree"
  printf "\`\`\`\n\n"
  printf "## MVCC Tuple Metadata\n\n"
  printf "\`\`\`text\n"
  cat "$mvcc"
  printf "\`\`\`\n\n"
  printf "## WAL Bytes From Update\n\n"
  printf "\`\`\`text\n"
  cat "$wal"
  printf "\`\`\`\n"
} > "$OUTPUT"

printf "Wrote %s\n" "$OUTPUT"
