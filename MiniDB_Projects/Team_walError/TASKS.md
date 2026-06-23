# TASKS.md — MiniDB Build Progress (Team walError)

> Live source of truth for progress. **Update BEFORE and AFTER every module.**
> After any context compaction: re-read CLAUDE.md + this file, then run
> `./.venv/bin/python -m pytest -q` to confirm on-disk reality before continuing.

## Status snapshot
- **Last completed:** wal.py + recovery (feat #6, crown jewel) + full engine integration
  of transactions/locks/WAL. ALL 6 CORE FEATURES DONE. Crash demo proven in tests:
  committed survives, uncommitted lost, rollback durable, failed-stmt atomic. 143 tests green.
- **Currently working on:** Track C LSM (extension, 20%).
- **Done so far:** ALL CORE (#1-#6) COMPLETE — Storage, B+tree, SQL exec, Optimizer,
  Txns(2PL+deadlock), WAL+Recovery. Engine: autocommit + explicit BEGIN/COMMIT/ROLLBACK,
  table-level S/X locking, deadlock->victim abort, WAL-as-truth recovery. 143 tests.
- **Next 3 steps:**
  1. `lsm.py` MemTable + WAL + immutable flush to SSTables + per-SSTable Bloom filter
     + size-tiered compaction; clean get/put/delete read+write paths + tests.
  2. `benchmarks/run_all.py` LSM vs B+tree/heap (write tput, read latency, space amp) +
     matplotlib charts + benchmarks/README.md analysis.
  3. demos/*.py (storage, btree, query, optimizer, transactions, recovery, lsm) narrated.

## How to run (for graders / fresh checkout)
```
python -m venv .venv && ./.venv/bin/pip install -e ".[dev]"
./.venv/bin/python -m pytest -q
./.venv/bin/python -m minidb.cli          # REPL
```

## Needs user input (non-blocking — placeholders resolved)
- [x] TEAM_NAME = walError
- [x] Member = Archisman Midya | archisman.23bcs10027@sst.scaler.com | 10027
- [x] PRIMARY_ROLL = 10027
- (Solo team; guidelines suggest 2–4 — accepted by user.)

## Resume commands
```
cd /Users/archismanmidya/Desktop/ADBMS/scaler-Adv-DBMS/MiniDB_Projects/Team_walError
./.venv/bin/python -m pytest -q          # run all tests
./.venv/bin/python demos/demo_storage.py # example demo (once built)
git -C /Users/archismanmidya/Desktop/ADBMS/scaler-Adv-DBMS log --oneline -10
```

## Checklist
### Scaffolding
- [x] Fork + clone + branch `dev` + venv + dirs
- [x] CLAUDE.md, TASKS.md, .gitignore
- [x] pyproject.toml + package __init__
- [x] Stub engine.py + cli.py runs end-to-end (6 smoke tests green)

### Core features (40% — protect first)
- [x] 1. Storage: constants/types/page → disk_manager → buffer_pool → heap (50 tests)
- [x] 2. B+ Tree index (search/insert/delete/splits/merge, range scan) — 15 tests
- [x] 3. SQL execution: sql.py parser + executor (CREATE/INSERT/SELECT+WHERE+JOIN/DELETE) — engine runs SQL end-to-end (113 tests)
- [x] 4. Cost-based optimizer (selectivity, join order, scan-vs-index, EXPLAIN) — 12 tests
- [x] 5. Transactions: lock_manager (2PL) + transaction (serializable) + deadlock detection — 10 tests
- [x] 6. Recovery: wal.py + redo recovery (crash→committed survive, uncommitted lost) — 8 tests

### Extension (20%)
- [x] Track C LSM: memtable+WAL, SSTable+bloom, size-tiered compaction (alongside heap) — 15 tests

### Benchmarks (15%)
- [x] benchmarks/run_all.py: LSM vs B+Tree vs heap (charts + results.json + README analysis) — 3 tests

### Demos
- [x] demo_storage, demo_btree, demo_query, demo_optimizer, demo_transactions, demo_recovery, demo_lsm (7 demos, smoke-tested)

### Tests
- [ ] pytest suite covering each core feature (all passing)

### Docs (deliverables)
- [x] README.md (12 sections) + Team Information
- [x] docs/ARCHITECTURE.md, docs/MODULES.md, docs/DATA_FLOW.md
- [x] System_Design_Docs/PostgreSQL_Internals/README.md (6 sections) — Deliverable 1

### Submission prep (do NOT open PRs)
- [ ] Branch `feature/SCALER_10027` (System Design only), title `SCALER_10027`
- [ ] Branch `feature/TEAM_walError` (MiniDB only), title `TEAM_walError`

## Known issues / notes
- Repo root has prior C++ lab stubs (storage_buffer, index, query_parser) — ignore.
- Reference PDFs are one level up in /Users/archismanmidya/Desktop/ADBMS/.
