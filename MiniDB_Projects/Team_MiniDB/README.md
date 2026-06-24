# MiniDB — Capstone Project (Team of 3)

**Extension Track:** Track A — Performance (Batch Processing)

## Team Members

| Member | Name | Roll Number | Email | Contribution |
|--------|------|-------------|-------|--------------|
| 1 | MD Kaif Molla | 24BCS10221 | md.24bcs10221@sst.scaler.com | Storage layer |
| 2 | Daksh Shah | 24BCS10092 | shah.24bcs10092@sst.scaler.com | Index, Catalog, Parser, Optimizer |
| 3 | Sourabh Srivastav | 24BCS10157 | sourabh.24bcs10157@sst.scaler.com | Execution, Transactions, Recovery, Docs |

> Fill in all names before submitting PR.

---

## Current Status

| Module | Owner | Status |
|--------|-------|--------|
| Storage (Page, BufferPool, HeapFile) | Member 1 | Done |
| B+ Tree, Catalog, Parser, Optimizer | Member 2 | Done |
| Executor, 2PL, WAL, Integration | Member 3 | Done |
| README, Viva doc, Benchmarks | Member 3 | Done |

---

## Build (Member 1 demo)

```bash
cd MiniDB_Projects/Team_MiniDB
make
./minidb
```

Expected output: inserts 2 rows and scans them from heap file.

---

## Full system (after all members commit)

After Member 2 and 3 push their code:

```bash
make
./minidb
```

Then SQL works: CREATE TABLE, INSERT, SELECT, DELETE, BEGIN/COMMIT, CRASH/RECOVER.


---


## Submission

- **Repo:** https://github.com/KnightKnight27/scaler-Adv-DBMS
- **PR title:** `TEAM_YourTeamName`
- **Folder:** `MiniDB_Projects/Team_MiniDB/`
