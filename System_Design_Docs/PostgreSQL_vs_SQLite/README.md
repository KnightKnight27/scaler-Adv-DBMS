# Topic 1: PostgreSQL vs. SQLite Architecture Comparison

## 1. Problem Background
PostgreSQL and SQLite represent two divergent philosophies in database engineering, fundamentally shaped by the specific problems they were designed to solve.

* **PostgreSQL:** Developed to solve the problem of robust, enterprise-grade data management across distributed networks. It addresses settings where hundreds or thousands of independent clients must modify shared data concurrently while demanding strict data integrity, complex relational operations, and zero data loss.

* **SQLite:** Conceived by D. Richard Hipp in 2000 to bypass the operational overhead of managing separate database server daemons for local application states. It was engineered to replace simple file formats (like CSVs or custom binary states) with standard SQL, prioritizing a zero-configuration footprint directly inside an application's process space.

## 2. Architecture Overview
The core difference between these engines lies in their runtime deployment and process models.

### PostgreSQL (Client-Server Architecture)

PostgreSQL follows a heavy, multi-process architecture. A master process (```postgres```/```postmaster```) binds to a network socket and listens for incoming connections. For every client connection request, the master process executes a ```fork()``` system call to spawn an isolated backend worker process dedicated exclusively to that connection. Communication between these worker processes occurs through an orchestrated shared memory segment (```shared_buffers```).

### SQLite (Embedded Database Design)

SQLite completely lacks network layers, server daemons, or background workers. It is an embedded database, meaning its engine exists purely as a stateless C library compiled directly into the address space of the host program. The database exists as a single ordinary file on disk, and all data processing passes through functional API calls executing on the host application's active thread.

## 3. Internal Design
Because their structural deployment differs, their low-level components diverge significantly across storage, memory, and concurrency handling:

* **Database File Organization & Storage Engine:** PostgreSQL relies on an append-only heap file architecture for tables, maintaining secondary indices (such as B-Trees) as separate, detached disk files. SQLite utilizes a unified B-Tree architecture where both system tables (using clustered B+Trees) and indices (using B-Trees) share space inside a single monolithic binary file.

* **Page Layout:** Both engines split files into distinct pages. PostgreSQL defaults to 8KB pages, matching typical Linux file system blocks. SQLite optimizes for lightweight environments, historically defaulting to 1KB or 4KB page sizes.

* **Concurrency Control (MVCC vs. File Locking):** PostgreSQL utilizes Multi-Version Concurrency Control (MVCC). Rows are never mutated in-place; modifications create new versions of rows tracked by transaction headers. Consequently, readers never block writers, and writers never block readers. SQLite enforces synchronization using coarse-grained file-level locks handled through the host operating system's VFS (Virtual File System) layer. Even under modern Write-Ahead Logging (WAL) modes, SQLite restricts execution to multiple readers or a single exclusive writer across the entire database file.

## 4. Design Trade-Offs
### Latency vs. Scalability
SQLite completely avoids the computational overhead of network protocols, TCP/IP loopbacks, and inter-process serialization. For localized, single-user reads, it delivers raw execution speeds exceeding PostgreSQL. However, because it lacks granular row locks, it scales poorly in multi-threaded or multi-user write scenarios. PostgreSQL introduces network and process boundaries that add latency penalties to basic transactions but scales seamlessly to handle complex concurrent write pipelines across multiple CPU cores.

### Operational Footprint
PostgreSQL demands rigorous database administration, continuous tuning parameters (```shared_buffers```, ```work_mem```), memory pool management, and background health monitoring. SQLite operates entirely configuration-free; it uses no extra memory beyond what the application allocates, requiring no active maintenance or server administration.

## 5. Experiments / Observations
### Experiment A: Process Model Observation
The PostgreSQL daemon was initialized inside the WSL environment and evaluated using the process status utility:

```bash
sudo service postgresql start
ps aux | grep postgres
```

#### Terminal Log Output:

```bash
postgres     266  0.1  0.3 220292 30720 ?        Ss   12:30   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres     282  0.0  0.1 220424  8380 ?        Ss   12:30   0:00 postgres: 16/main: checkpointer
postgres     283  0.0  0.0 220444  6588 ?        Ss   12:30   0:00 postgres: 16/main: background writer
postgres     307  0.0  0.1 220292 10044 ?        Ss   12:30   0:00 postgres: 16/main: walwriter
postgres     308  0.0  0.1 221888  8508 ?        Ss   12:30   0:00 postgres: 16/main: autovacuum launcher
postgres     309  0.0  0.1 221872  7868 ?        Ss   12:30   0:00 postgres: 16/main: logical replication launcher
```

#### Architectural Analysis:
The observation confirms PostgreSQL’s multi-process design. Process ID ```266``` represents the root server process. It relies on explicit, dedicated background helper processes (```checkpointer```, ```background writer```, ```walwriter```, ```autovacuum launcher```) to manage disk syncing, transaction journaling, and clean up operations asynchronously.

### Experiment B: Concurrency Crash Test
A shell script was executed in WSL to run 20 background processes concurrently inserting rows into an SQLite table, testing its limits:

```bash
#!/bin/bash
sqlite3 test.db "CREATE TABLE IF NOT EXISTS stress (id INTEGER PRIMARY KEY, val TEXT);"
for i in {1..20}; do
  sqlite3 test.db "INSERT INTO stress (val) VALUES ('test_data');" &
done
wait
echo "Done"
```

#### Terminal Log Output:
```bash
[1] 941
...
[20] 960
Error: stepping, database is locked (5)
Error: stepping, database is locked (5)
Error: stepping, database is locked (5)
...
[1]   Exit 5                  sqlite3 test.db "INSERT INTO stress (val) VALUES ('test_data');"
[20]+  Exit 5                  sqlite3 test.db "INSERT INTO stress (val) VALUES ('test_data');"
Done
```

#### Architectural Analysis:
The frequent Error: stepping, database is locked (5) outcomes illustrate SQLite’s primary architectural limitation. When a process attempts an INSERT, SQLite requests an Exclusive Lock on the underlying database file. Because multiple asynchronous bash worker processes attempted simultaneous updates, the physical file lock was withheld, throwing an immediate code 5 (SQLITE_BUSY) rejection. This shows why SQLite is fundamentally unsuited for multi-user, write-heavy systems.

## 6. Key Learnings
* **The Blueprint Follows the Target Domain:** Neither system is objectively superior. SQLite's design choices make it highly effective for embedded systems, edge computing, mobile clients, and local files where isolation and light footprints matter most. PostgreSQL sacrifices simplicity to support heavy corporate networks, high concurrency, and massive data scale.

* **The Hidden Cost of High Availability:** Observing PostgreSQL's process list demonstrates that high write concurrency requires massive background support infrastructure. Managing isolated row visibility (MVCC) demands dedicated subsystems for log management, checkpoint flushing, and automatic row garbage collection (autovacuum).