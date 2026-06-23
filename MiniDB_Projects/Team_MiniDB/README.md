# MiniDB — Capstone Project (Team of 3)

**Extension Track:** Track A — Performance (Batch Processing)

## Team Members

| Member | Name | Roll Number | Email | Contribution |
|--------|------|-------------|-------|--------------|
| 1 | YOUR NAME | 24BCSXXXXX | you@scaler.com | Storage layer |
| 2 | TEAMMATE 2 | | | Index, Catalog, Parser, Optimizer |
| 3 | TEAMMATE 3 | | | Execution, Transactions, Recovery, Docs |

> Fill in all names before submitting PR.

---

## Current Status

| Module | Owner | Status |
|--------|-------|--------|
| Storage (Page, BufferPool, HeapFile) | Member 1 | Done |
| B+ Tree, Catalog, Parser, Optimizer | Member 2 | Pending |
| Executor, 2PL, WAL, Integration | Member 3 | Pending |
| README, Viva doc, Benchmarks | Member 3 | Pending |

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

## Team workflow

See **[TEAM_PR_GUIDE.md](TEAM_PR_GUIDE.md)** for exact git steps and PR instructions.

---

## Submission

- **Repo:** https://github.com/KnightKnight27/scaler-Adv-DBMS
- **PR title:** `TEAM_YourTeamName`
- **Folder:** `MiniDB_Projects/Team_MiniDB/`
