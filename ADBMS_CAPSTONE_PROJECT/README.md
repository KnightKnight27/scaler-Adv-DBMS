# MiniDB

MiniDB is a lightweight, fully functional database management system written in C++17 for the Advanced DBMS Capstone Project. It demonstrates core database internal architecture, adhering to academic requirements.

## Core Architecture

- **Storage Engine**: 4KB page-based heap files driven by a `DiskManager` and LRU `BufferPool`.
- **Indexing**: A disk-backed **B+ Tree** for dynamic O(log N) searches and insertions.
- **Query Execution**: Volcano Iterator Model (`SeqScan`, `IndexScan`, `NestedLoopJoin`, `Insert`, `Delete`).
- **Query Optimization**: Rule-based heuristic planner that automatically selects between sequential scans and index scans.
- **Transaction Management**: Strict Two-Phase Locking (2PL).
- **Extension Track**: **Distributed Replication** via Statement-Based Replication to a secondary backend node.
- **Logging & Recovery**: Write-Ahead Logging (WAL) and an ARIES-lite Redo/Undo crash recovery loop.

## Compilation

Ensure you have a modern C++ compiler (`g++` or `clang++` supporting C++17) installed.

### Using CMake
```bash
mkdir build
cd build
cmake ..
cmake --build .
./minidb
```

### Using G++ directly (MinGW/Linux)
```powershell
g++ -std=c++17 -static -Isrc src/**/*.cpp -o minidb.exe
.\minidb.exe
```

## Running the CLI

Run the executable to launch the interactive REPL. The REPL supports basic SQL syntax:

```sql
MiniDB > INSERT INTO users VALUES (1, Alice)
Inserted 1 rows.

MiniDB > INSERT INTO users VALUES (2, Bob)
Inserted 1 rows.

MiniDB > SELECT * FROM users
| ID: 1 | Name: ALICE |
| ID: 2 | Name: BOB |
2 rows returned.

MiniDB > SELECT * FROM users WHERE id = 1
| ID: 1 | Name: ALICE |
1 rows returned.

MiniDB > DELETE FROM users WHERE id = 1
Deleted 1 rows.

MiniDB > exit
```

## Further Documentation

- **[Viva Demo Guide](viva_demo.md)**: A step-by-step presentation script for the capstone viva.
- **[Project Status Report](PROJECT_STATUS_REPORT.md)**: A comprehensive technical audit mapping all capstone requirements to the implementation.

# MiniDB

## Team Information

### Team Name
MiniDBMasters

### Team Members

Abhijit P
Email: abhijit.24bcs10175@sst.scaler.com

Romit Raj Sahu
Email: romit.24bcs10436@sst.scaler.com

Manthan Kalra
Email: manthan.24bcs10478@sst.scaler.com

Naman Kajla
Email: naman.24bcs10223@sst.scaler.com