# MiniDB Track B - Concurrency

MiniDB is an educational relational database engine for the Advanced DBMS capstone. The chosen extension is **Track B: Concurrency**, implemented by adding MVCC on top of the required core 2PL transaction path.

## Build

Requires CMake and a C++20 compiler. On Windows, install **Visual Studio Build Tools**
with the **Desktop development with C++** workload, then run these commands from a
Developer PowerShell or Developer Command Prompt.

If an earlier configure failed, remove the incomplete build directory first:

```powershell
Remove-Item -Recurse -Force build
```

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

If you are not in a developer shell, use the Visual Studio generator explicitly:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## M1 Verification

```powershell
ctest --test-dir build --output-on-failure
```

## Milestone Status

- M1 Page manager + buffer pool integrated: implemented.
- M2 B+ tree + parser connected: implemented.
- M3 Query execution engine with joins and aggregation: implemented.
- M4 Transactions and locking: planned.
- M5 Recovery, benchmarking, and final demo: planned.
- Track B MVCC extension: planned after the required 2PL transaction baseline.

## Required Core Features

The codebase currently includes the storage-engine foundation: page-based heap files, page manager behavior, page reads/writes, and buffer pool usage. M2 adds a primary-key B+ tree and structured SQL parsing for `INSERT`, `SELECT`, and `DELETE`. M3 adds heap-backed query execution with primary-key index lookup, deletes, nested-loop joins, and `COUNT(*)` aggregation. Later milestones will add cost-based optimization, 2PL transaction management, WAL recovery, and MVCC extension support.
