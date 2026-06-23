# CLAUDE.md — MiniDB Capstone Project Memory (Team walError)

> **READ THIS FILE AND `TASKS.md` IN FULL BEFORE DOING ANY WORK — especially
> after a context compaction or at the start of a new session.** Then verify
> on-disk reality by running the tests (`./.venv/bin/python -m pytest -q` from
> this dir) rather than trusting memory. `TASKS.md` is the live source of truth
> for progress; this file holds stable facts and conventions.

## Team
- **Team name:** walError  → project dir `MiniDB_Projects/Team_walError/`
- **Member (solo):** Archisman Midya | archisman.23bcs10027@sst.scaler.com | Roll 10027
- Note: course guidelines suggest 2–4 members; this team is solo by the user's choice.

## What we are building
Two **separate** deliverables for the Advanced DBMS course, each its own GitHub
Pull Request to the fork `Om-Midya/scaler-Adv-DBMS` (upstream
`KnightKnight27/scaler-Adv-DBMS`). **Do NOT open PRs** — prepare branches/commits
only and stop.

### Deliverable 1 — System Design research doc (no engine code)
- Topic: **PostgreSQL Internal Architecture** (Topic 2).
- Path: `System_Design_Docs/PostgreSQL_Internals/README.md`
- 6 required sections: Problem Background, Architecture Overview, Internal Design
  (Buffer Manager, nbtree B-Tree, MVCC, WAL), Design Trade-Offs,
  Experiments/Observations (EXPLAIN ANALYZE), Key Learnings.
- PR branch: `feature/SCALER_10027` ; PR title EXACTLY `SCALER_10027`

### Deliverable 2 — Capstone: MiniDB (this directory)
- Working relational DB engine in **Python 3.11+** (dev machine 3.14.4), built
  from scratch, stdlib-first.
- Extension track: **Track C — Modern Storage (LSM-tree)**, built ALONGSIDE the
  heap engine (not replacing it) so the core stays stable.
- PR branch: `feature/TEAM_walError` ; PR title EXACTLY `TEAM_walError`

## Tech stack & conventions
- Python 3.11+. Standard library where possible. Third-party allowed: `pytest`
  (tests), `matplotlib` (benchmark charts). No external DB libraries.
- venv at `.venv` (gitignored). Run via `./.venv/bin/python` from this dir.
- Run tests: `./.venv/bin/python -m pytest -q`
- Run a demo: `./.venv/bin/python demos/demo_storage.py`
- Readable, viva-explainable code. Small modules, clear names, docstrings. Never
  claim something works without running it and pasting real output.

## Repository map (this project)
```
MiniDB_Projects/Team_walError/
  CLAUDE.md            # this file (stable project memory)
  TASKS.md             # live progress / task list (UPDATE CONSTANTLY)
  README.md            # 12-section capstone report (deliverable)
  pyproject.toml       # package + pytest config
  .gitignore
  src/minidb/          # the engine
    constants.py       # PAGE_SIZE and tunables
    types.py           # column types, Schema, Row/tuple encoding
    page.py            # slotted page layout
    disk_manager.py    # read/write/allocate pages in one DB file
    buffer_pool.py     # buffer pool + clock-sweep replacement, dirty flush
    heap.py            # heap table (tuples across pages), RID
    btree.py           # B+ tree index: key -> RID, search/insert/delete/splits
    catalog.py         # system catalog: tables, schemas, indexes, stats
    sql.py             # tokenizer + parser + AST
    plan.py            # logical/physical plan + cost-based optimizer
    executor.py        # Volcano iterator operators
    lock_manager.py    # 2PL locks + deadlock detection (wait-for graph)
    transaction.py     # transaction manager, serializable isolation
    wal.py             # write-ahead log + redo recovery
    lsm.py             # LSM: memtable, SSTable, bloom filter, compaction
    engine.py          # Database facade: execute(sql) end-to-end
    cli.py             # REPL entry point
  tests/  demos/  benchmarks/  docs/
```

## Architecture summary (data flow)
SQL string → `sql.py` (tokenize → parse → AST) → `plan.py` (logical plan →
optimizer picks SeqScan vs IndexScan, join order) → `executor.py` (Volcano
operators pull tuples) → `heap.py`/`btree.py` (via `catalog.py`) →
`buffer_pool.py` → `disk_manager.py` (single DB file). Writes go through
`wal.py` (log-before-write) and take locks via `lock_manager.py` under a
`transaction.py` context. Recovery replays the WAL on startup.

## Build order (each module gets a passing pytest test before moving on)
1. cli/engine stub (end-to-end skeleton) 2. constants/types/page 3. disk_manager
4. buffer_pool 5. heap 6. btree 7. catalog 8. sql 9. executor 10. plan/optimizer
11. lock_manager + transaction 12. wal + recovery 13. lsm 14. benchmarks
15. demos 16. docs (ARCHITECTURE/MODULES/DATA_FLOW) + README
17. System Design doc (Deliverable 1)

## Grading priority (protect in this order if constrained)
Core features (40%) > Extension/LSM (20%) > Benchmarks (15%) > Code quality (15%)
> Demo & report (10%). A correct, explainable, end-to-end core beats a broad but
broken system. The recovery demo is the crown jewel — make it provably correct.

## Git workflow
- Working branch: `dev`. Commit after each module's tests pass, clear messages.
- At the end, prepare two clean submission branches:
  `feature/SCALER_10027` (System Design only) and `feature/TEAM_walError` (MiniDB
  only), each containing only its deliverable. Set the exact PR titles above.
- Commit as the local git user. Do NOT push to upstream or open PRs.
