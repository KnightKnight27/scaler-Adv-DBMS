# Lab 7 — Transaction Manager: MVCC + Two-Phase Locking + Deadlock Detection

## Parts

**MVCC (Multi-Version Concurrency Control)**  
Every write creates a new row version (`xmin`/`xmax`). Readers walk the version chain and return the first version visible under their snapshot XID — no reader ever blocks a writer.

**Strict 2PL (Two-Phase Locking)**  
All locks are held until commit/abort (shrinking phase = transaction end). Shared locks allow concurrent reads; exclusive locks serialize writes.

**Deadlock Detection**  
A waits-for graph is maintained per lock request. A DFS cycle check runs before any transaction blocks — if a cycle is found, the requesting transaction is aborted immediately.

| Everything is implemented in a single code file `txmgr.cpp`

## Build & Run

```bash
g++ -std=c++14 -o txmgr txmgr.cpp
./txmgr
```

## Scenarios

| # | What it tests |
|---|--------------|
| 1 | MVCC snapshot isolation — t2 sees pre-t3 value despite t3 committing first |
| 2 | Two shared locks on same row granted simultaneously |
| 3 | Exclusive lock blocks a concurrent reader until commit |
| 4 | Deadlock (A waits B, B waits A) — one transaction aborted by cycle detection |
