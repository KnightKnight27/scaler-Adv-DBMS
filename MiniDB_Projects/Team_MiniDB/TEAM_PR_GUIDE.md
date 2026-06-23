# Team PR Guide — 3 Members

**Repo:** https://github.com/KnightKnight27/scaler-Adv-DBMS  
**PR Title:** `TEAM_YourTeamName`  
**Deadline:** 23 June 2026, 11:59 PM IST

---

## Team Split (3 members)

| Member | Role | Folders / Files | Commit message example |
|--------|------|-----------------|------------------------|
| **Member 1 (You)** | Storage Lead | `src/common/`, `src/storage/`, `Makefile` | `feat: add storage layer (page, buffer pool, heap file)` |
| **Member 2** | Query Layer | `src/index/`, `src/catalog/`, `src/parser/`, `src/optimizer/` | `feat: add B+ tree, catalog, parser and optimizer` |
| **Member 3** | Txn + Integration | `src/execution/`, `src/transaction/`, `src/recovery/`, `src/performance/`, `Database.cpp/hpp`, `main.cpp` SQL REPL, docs, benchmarks | `feat: add executor, 2PL, WAL recovery and integration` |

---

## Step-by-step (do in this order)

### Step 1 — Member 1 (YOU) — today

```bash
cd scaler-Adv-DBMS
git checkout -b team-minidb
git add MiniDB_Projects/Team_MiniDB/
git commit -m "feat(storage): add page manager, buffer pool and heap file - Member 1"
git push -u origin team-minidb
```

Tell Member 2 and 3: *"Pull branch `team-minidb` and add your folders."*

---

### Step 2 — Member 2

```bash
git clone https://github.com/KnightKnight27/scaler-Adv-DBMS.git
cd scaler-Adv-DBMS
git checkout team-minidb
git pull

# Add your files under:
#   src/index/BPlusTree.hpp, BPlusTree.cpp
#   src/catalog/Catalog.hpp, Catalog.cpp
#   src/parser/Lexer.hpp, Lexer.cpp, Parser.hpp, Parser.cpp
#   src/optimizer/Optimizer.hpp, Optimizer.cpp

# Use: using namespace std;  (NOT std:: everywhere)

make
git add MiniDB_Projects/Team_MiniDB/src/index/
git add MiniDB_Projects/Team_MiniDB/src/catalog/
git add MiniDB_Projects/Team_MiniDB/src/parser/
git add MiniDB_Projects/Team_MiniDB/src/optimizer/
git commit -m "feat(query): add B+ tree, catalog, parser and optimizer - Member 2"
git push origin team-minidb
```

**Member 2 viva topics:** B+ tree search, parser tokens, index vs table scan choice.

---

### Step 3 — Member 3

```bash
git checkout team-minidb
git pull

# Add your files under:
#   src/execution/Executor.hpp, Executor.cpp
#   src/transaction/LockManager.hpp/cpp, TransactionManager.hpp/cpp
#   src/recovery/WAL.hpp/cpp, RecoveryManager.hpp/cpp
#   src/performance/BatchExecutor.hpp
#   src/Database.hpp, Database.cpp
#   Update src/main.cpp to SQL REPL
#   Update Makefile and CMakeLists.txt with all sources
#   Add README.md (final), README_VIVA.md, docs/, benchmarks/

make
git add .
git commit -m "feat(integration): add executor, 2PL, WAL recovery and docs - Member 3"
git push origin team-minidb
```

**Member 3 viva topics:** 2PL, deadlock, WAL REDO/UNDO, batch processing (Track A).

---

### Step 4 — Open Pull Request (any member)

1. Go to https://github.com/KnightKnight27/scaler-Adv-DBMS
2. Click **Pull requests → New pull request**
3. Base: `main` ← Compare: `team-minidb`
4. Title: **`TEAM_YourTeamName`**
5. Description must include:

```
Team Name: Your Team Name

Member 1:
  Name: ...
  Roll: ...
  Email: ...

Member 2:
  Name: ...
  Roll: ...
  Email: ...

Member 3:
  Name: ...
  Roll: ...
  Email: ...

Extension Track: Track A — Performance (Batch Processing)
```

---

## Code style rule (all members)

```cpp
#include <iostream>
#include <string>
using namespace std;   // YES — use this

// NOT: std::cout, std::string everywhere
```

Keep `namespace minidb { ... }` for your classes.

---

## What each member must explain in viva

| Member | Must explain |
|--------|--------------|
| Member 1 | 4KB pages, buffer pool LRU, heap file, page layout |
| Member 2 | B+ tree insert/search, SQL parser, optimizer scan choice |
| Member 3 | Nested loop join, 2PL locks, WAL + recovery, batch mode |

---

## README checklist (Member 3 fills before PR)

- [ ] Project overview + extension track
- [ ] Architecture diagram
- [ ] Storage, Index, Query, Optimizer, Transactions, Recovery sections
- [ ] Benchmark results
- [ ] How to run (`make && ./minidb`)
- [ ] Team member table with roll numbers

---

## Send to teammates

Share the `reference_code/` folder (on WhatsApp/Drive) with Member 2 and 3 — it has the full implementation they should add and **understand**, not copy blindly.

Each member must be able to explain their own files in the viva.
