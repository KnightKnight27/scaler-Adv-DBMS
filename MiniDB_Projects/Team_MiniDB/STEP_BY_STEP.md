# MiniDB — Simple Step-by-Step (Team of 3)

**Deadline:** 23 June 2026, 11:59 PM IST  
**Submit PR to:** https://github.com/KnightKnight27/scaler-Adv-DBMS

---

## BEFORE YOU START (all 3 members)

1. Each person needs a **GitHub account**
2. Each person installs **git** and **g++** (C++ compiler)
3. Decide your **team name** (e.g. `Team_DBWarriors`)
4. Fill names in `README.md`

---

# PART A — MEMBER 1 (YOU — Kaif)

### Step A1: Test your code works

```bash
cd MiniDB_Projects/Team_MiniDB
make
./minidb
```

You should see Alice and Bob rows printed.

### Step A2: Create branch and commit (already done if you ran our script)

```bash
cd /path/to/scaler-Adv-DBMS
git checkout -b team-minidb
git add MiniDB_Projects/Team_MiniDB/
git commit -m "feat(storage): add page manager, buffer pool and heap file - Member 1"
```

### Step A3: Push to GitHub

```bash
git push -u origin team-minidb
```

### Step A4: Share with teammates

Send them on WhatsApp:
1. Link: `https://github.com/WhySeriousKaif/scaler-Adv-DBMS` (or your fork)
2. Branch name: `team-minidb`
3. Folder: `reference_code/` inside Team_MiniDB (zip and send)
4. Tell them to read `TEAM_PR_GUIDE.md`

### Step A5: Viva prep (your part)

Be ready to explain:
- Why pages are 4KB
- How buffer pool LRU works
- How heap file stores rows

---

# PART B — MEMBER 2

### Step B1: Clone and get branch

```bash
git clone https://github.com/WhySeriousKaif/scaler-Adv-DBMS.git
cd scaler-Adv-DBMS
git checkout team-minidb
git pull
```

### Step B2: Copy reference files into project

Copy from `reference_code/member2/` (Member 1 sends you this zip) into:

```
MiniDB_Projects/Team_MiniDB/src/index/
MiniDB_Projects/Team_MiniDB/src/catalog/
MiniDB_Projects/Team_MiniDB/src/parser/
MiniDB_Projects/Team_MiniDB/src/optimizer/
```

**Important:** Read every file. Change variable names slightly if you want. You must explain it in viva.

### Step B3: Test (will NOT fully run yet — Member 3 needed)

```bash
cd MiniDB_Projects/Team_MiniDB
# Just check files exist
ls src/index/ src/catalog/ src/parser/ src/optimizer/
```

### Step B4: Commit and push

```bash
cd ../..   # back to repo root
git add MiniDB_Projects/Team_MiniDB/src/index/
git add MiniDB_Projects/Team_MiniDB/src/catalog/
git add MiniDB_Projects/Team_MiniDB/src/parser/
git add MiniDB_Projects/Team_MiniDB/src/optimizer/
git commit -m "feat(query): add B+ tree, catalog, parser and optimizer - Member 2"
git push origin team-minidb
```

### Step B5: Viva prep (Member 2)

Explain: B+ tree search, parser tokens, when optimizer picks index scan.

---

# PART C — MEMBER 3

### Step C1: Pull latest

```bash
git clone https://github.com/WhySeriousKaif/scaler-Adv-DBMS.git
cd scaler-Adv-DBMS
git checkout team-minidb
git pull
```

### Step C2: Copy reference files

Copy from `reference_code/member3/` into:

```
src/execution/
src/transaction/
src/recovery/
src/performance/
src/Database.hpp
src/Database.cpp
```

Also replace:
- `src/main.cpp` (SQL REPL version)
- `Makefile` (full version)
- `CMakeLists.txt` (full version)

Add docs:
- `README.md` (complete)
- `README_VIVA.md`
- `docs/architecture.md`

### Step C3: Build and test full system

```bash
cd MiniDB_Projects/Team_MiniDB
make
./minidb
```

Try:
```sql
CREATE TABLE employee (id INT PRIMARY KEY, name STRING, age INT);
INSERT INTO employee VALUES (1, Alice, 30);
SELECT * FROM employee WHERE id = 1;
```

### Step C4: Commit and push

```bash
git add MiniDB_Projects/Team_MiniDB/
git commit -m "feat(integration): add executor, 2PL, WAL recovery and docs - Member 3"
git push origin team-minidb
```

### Step C5: Viva prep (Member 3)

Explain: 2PL, WAL REDO/UNDO, nested loop join, batch mode.

---

# PART D — OPEN FINAL PR (any member, after all 3 pushed)

### Step D1: Fork official repo (once per team)

1. Open https://github.com/KnightKnight27/scaler-Adv-DBMS
2. Click **Fork** (top right)
3. Push your `team-minidb` branch to YOUR fork if not already there

### Step D2: Create Pull Request

1. Go to https://github.com/KnightKnight27/scaler-Adv-DBMS
2. Click **Pull requests** → **New pull request**
3. Click **compare across forks**
4. Base: `KnightKnight27/scaler-Adv-DBMS` → `main`
5. Head: `YOUR_USERNAME/scaler-Adv-DBMS` → `team-minidb`
6. Title: **`TEAM_YourTeamName`**
7. Paste this in description:

```
Team Name: Your Team Name
Extension Track: Track A — Performance (Batch Processing)

Member 1:
  Name: Kaif
  Roll: 24BCS10221
  Email: your@email.com
  Contribution: Storage layer

Member 2:
  Name: ...
  Roll: ...
  Email: ...
  Contribution: Index, Parser, Optimizer

Member 3:
  Name: ...
  Roll: ...
  Email: ...
  Contribution: Execution, Transactions, Recovery
```

8. Click **Create pull request**

---

# CHECKLIST BEFORE DEADLINE

- [ ] All 3 members pushed to `team-minidb`
- [ ] `make && ./minidb` works with SQL
- [ ] README.md has all 3 names + roll numbers
- [ ] PR created on KnightKnight27 repo
- [ ] Each member read README_VIVA.md for their section

---

# IF STUCK

| Problem | Fix |
|---------|-----|
| `git push` denied | Ask repo owner to add you as collaborator |
| `make` fails | Run `make clean && make`, read error line |
| Merge conflict | Person who pushed last runs `git pull` first |
| Don't understand code | Read comments in file + README_VIVA.md |
