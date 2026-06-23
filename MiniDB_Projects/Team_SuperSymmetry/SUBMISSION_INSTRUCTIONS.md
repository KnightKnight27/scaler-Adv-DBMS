# SUBMISSION INSTRUCTIONS — read before you submit

This folder is a complete, working MiniDB capstone. Everything runs and the
tests pass. A few things are **your job** before you push the PR.

## 1. Make it yours (REQUIRED)
- [ ] Rename the folder `Team_QueryForge` to your real team name.
- [ ] Edit `README.md` line 4: replace the team name and add every member's
      **name, roll number, and email**.
- [ ] Skim `git`/PR template requirements in the course repo
      (https://github.com/KnightKnight27/scaler-Adv-DBMS) and match its expected
      directory layout (`MiniDB_Projects/Team_<NAME>/...`).

## 2. Verify on your machine (REQUIRED)
Run these from inside the team folder — all should succeed:
```bash
python tests/test_minidb.py        # expect "12/12 tests passed"
python demos/demo_recovery.py
python demos/demo_concurrency.py
python demos/demo_optimizer.py
python benchmarks/bench.py          # regenerates benchmarks/results.md
```
Python 3.10+ required; no third-party packages.

## 3. Understand it for the viva (REQUIRED — the course mandates this)
AI assistance is allowed **only if your team can explain and defend the code.**
Do not submit code you can't walk through. Highest-probability questions:
- **WAL recovery** (`wal.py` → `recover`): why redo *then* undo, what a "loser"
  is, how the page LSN makes redo idempotent, and what "steal" means.
- **MVCC visibility** (`mvcc.py` → `_visible`, `commit`): how a snapshot is
  defined, why readers never block, and what first-committer-wins prevents.
- **Deadlock detection** (`lock_manager.py` → `_creates_cycle`): the wait-for
  graph and victim selection.
- **Optimizer** (`optimizer.py`): the SeqScan-vs-IndexScan cost comparison and
  selectivity estimation.

Tip: read each module top-to-bottom once, then run the matching demo and trace
the output back to the code.

## 4. What's implemented vs. not
See README §11 (Limitations). Know these cold — examiners love asking about
scope boundaries (in-memory B+ trees, phantom reads under row 2PL, in-memory
MVCC store, no `UPDATE` statement).

## What's in here
```
README.md                  full project report (12 required sections)
src/minidb/                17 source files (the engine + CLI)
demos/                     3 runnable demos (recovery, concurrency, optimizer)
benchmarks/bench.py        benchmark harness
benchmarks/results.md      generated benchmark results
tests/test_minidb.py       12 tests across all layers (all passing)
```
