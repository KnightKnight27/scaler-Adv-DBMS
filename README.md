# scaler-Adv-DBMS

Advanced DBMS lab submissions. Original fork files are kept in place; completed
runnable lab solutions are added alongside them.

| Lab | Topic | Location | Run |
|-----|-------|----------|-----|
| 1 | File I/O kernel journey | `file_io/reader.cpp`, `file_io/LAB1.md` | `cd file_io && g++ -std=c++17 -Wall -Wextra -pedantic -o reader reader.cpp && ./reader` |
| 2 | SQLite PRAGMA internals and PostgreSQL comparison | `sqlite_internals/` | `sqlite3 students.db ".read sqlite_internals/pragmas.sql"` |
| 3 | ClockSweep buffer replacement | `storage_buffer/clocksweep.cpp` | `cd storage_buffer && g++ -std=c++17 -Wall -Wextra -pedantic -o clocksweep clocksweep.cpp && ./clocksweep` |
| 4 | Red-Black Tree and B-Tree | `index/rbt.cpp`, `index/btree.cpp` | `cd index && g++ -std=c++17 -Wall -Wextra -pedantic -o rbt rbt.cpp && ./rbt && g++ -std=c++17 -Wall -Wextra -pedantic -o btree btree.cpp && ./btree` |
| 5 | Shunting-yard evaluator and SELECT executor | `query_parser/sql_engine.cpp` | `cd query_parser && g++ -std=c++17 -Wall -Wextra -pedantic -o sql_engine sql_engine.cpp && ./sql_engine` |
| 6 | MVCC, Strict 2PL, deadlock detection | `txn_manager/txn_manager.cpp` | `cd txn_manager && g++ -std=c++17 -Wall -Wextra -pedantic -pthread -o txmgr txn_manager.cpp && ./txmgr` |
