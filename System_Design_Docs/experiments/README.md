# Experiment scripts

These are the scripts that produced the **real, measured** outputs quoted in the four topic
`README.md` files. They are self-contained and write their scratch data next to themselves.

| Script | Used by | What it measures |
|---|---|---|
| `sqlite_exp.py` | PostgreSQL_vs_SQLite | SQLite page layout, `EXPLAIN QUERY PLAN`, index speedup, VDBE bytecode |
| `sqlite_exp2.py` | PostgreSQL_vs_SQLite, MySQL_InnoDB | `WITHOUT ROWID` (clustered) storage vs heap+secondary index; WAL checkpointing |
| `pg_exp.py` | PostgreSQL_vs_SQLite, PostgreSQL_Internals | `EXPLAIN (ANALYZE, BUFFERS)` join, seq→index scan, MVCC `xmin`/`ctid`, `pg_stats`, WAL volume |
| `pg_exp2.py` | PostgreSQL_Internals | bloat + `VACUUM` space reuse (full-table update) |
| `lsm_sim.py` | RocksDB | LSM write / read / space amplification, leveled vs tiered, Bloom-filter effect |

## Requirements

```bash
python --version          # 3.12 used here
# SQLite is built into Python's stdlib (and a sqlite3 3.45 CLI was also present)
pip install pgserver pg8000   # bundles a real PostgreSQL 16.2 server + pure-Python driver
```

`pgserver` downloads and runs a real PostgreSQL server locally (no system install / admin needed),
which is how the PostgreSQL experiments were run on a machine without `psql`/Docker.

## Run

```bash
python sqlite_exp.py
python sqlite_exp2.py
python pg_exp.py        # starts a bundled PostgreSQL 16.2, runs the suite
python pg_exp2.py
python lsm_sim.py
```

> Note: `pgserver`'s built-in `.psql()` helper breaks when the user profile path contains a space
> (e.g. `C:\Users\Varun Mundada\...`), so `pg_exp.py` connects through `pg8000` directly instead of
> shelling out to `psql`. Outputs in the topic READMEs are copied verbatim from these runs
> (PostgreSQL 16.2, SQLite 3.45).
