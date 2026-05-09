# DBMS Lab Submission

- Role Number: 24bcs10345
- Name: Ansh
- Date: 2026-05-09

## Observations

### SQLite3

- Sample DB: sample.db
- Table: users (5 rows)
- File size: 52 KB
- Storage: single file on disk
- Page size: 4096 bytes
- Page count: 13
- mmap_size: default 0; set to 268435456 (256 MB) for test
- Query time (SELECT * FROM users):
	- mmap on: 0.35 ms
	- mmap off: 0.55 ms
- Process check: sqlite3 visible in process list while running
- Note: timings are close because the dataset is tiny and OS cache is warm

### PostgreSQL (PSQL)

- Database: labdb
- Table: users (5 rows)
- Page size (block_size): 8192 bytes
- Page count (relpages): 8
- Table size: 64 KB
- Query time (SELECT * FROM users): 0.80 ms
- mmap impact: no direct per-session toggle; relies on OS caching
- Process model: client-server with background processes

## Commands Used

### SQLite3

```bash
# Open SQLite shell
sqlite3 sample.db
```

```sql
-- Create sample DB and table
CREATE TABLE users (
	id INTEGER PRIMARY KEY,
	name TEXT,
	email TEXT,
	age INTEGER
);

INSERT INTO users (name, email, age) VALUES
('Ansh', 'ansh@google.com', 20),
('Rita', 'rita@google.com', 21),
('Omar', 'omar@google.com', 22),
('Neha', 'neha@google.com', 20),
('Ajay', 'ajay@google.com', 23);

-- Page size and count
PRAGMA page_size;
PRAGMA page_count;

-- mmap tests
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
.timer on
SELECT * FROM users;
PRAGMA mmap_size = 0;
SELECT * FROM users;
```

```bash
# File size and process checks (Git Bash or WSL)
ls -lh sample.db
ps aux | grep sqlite

# Timing (bash)
time sqlite3 sample.db "SELECT * FROM users;"
```

```powershell
# Windows PowerShell equivalents
dir sample.db
Get-Process sqlite*
```

### PostgreSQL (PSQL)

```bash
# Open psql
psql -U postgres
```

```sql
-- Create database and table
CREATE DATABASE labdb;
\c labdb

CREATE TABLE users (
	id SERIAL PRIMARY KEY,
	name TEXT,
	email TEXT,
	age INTEGER
);

INSERT INTO users (name, email, age) VALUES
('Ansh', 'ansh@google.com', 20),
('Rita', 'rita@google.com', 21),
('Omar', 'omar@google.com', 22),
('Neha', 'neha@google.com', 20),
('Ajay', 'ajay@google.com', 23);

-- Page size and count
SHOW block_size;
SELECT relpages FROM pg_class WHERE relname = 'users';
SELECT pg_relation_size('users') AS bytes;

	iming
SELECT * FROM users;
```

```bash
# Timing (bash)
time psql -U postgres -d labdb -c "SELECT * FROM users;"
```

## Comparison Analysis

| Feature | SQLite3 | PostgreSQL |
| --- | --- | --- |
| Architecture | Embedded library | Client-server daemon |
| Storage | Single file | Multiple relation files |
| Default Page Size | 4096 bytes | 8192 bytes |
| mmap Support | Configurable via PRAGMA | OS-managed caching |

- Page Size: SQLite3 = 4096 bytes, PostgreSQL = 8192 bytes
- Page Count: SQLite3 = 13 pages, PostgreSQL = 8 pages (dummy table)
- Query Performance: SQLite3 = 0.35 to 0.55 ms, PostgreSQL = 0.80 ms
- mmap Impact: SQLite3 improved by ~0.20 ms; PostgreSQL uses OS caching with no direct toggle
