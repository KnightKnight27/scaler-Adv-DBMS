# PostgreSQL vs SQLite

| Area | SQLite | PostgreSQL |
|------|--------|------------|
| Runtime model | embedded library inside the application | separate database server process |
| Communication | direct function calls and file I/O | TCP or Unix socket protocol |
| Storage | one portable `.db` file made of pages | data directory with relation files and WAL |
| Concurrency | file locks, with one writer at a time | MVCC with many concurrent readers and writers |
| Cache | SQLite page cache plus the OS page cache, optional mmap | server-managed shared buffers plus OS cache |
| Security | filesystem permissions | roles, passwords, SSL, and `pg_hba.conf` |
| Types | dynamic type affinity | strict, rich type system |
| Operations | simple deployment, almost no administration | service management, tuning, backup, replication |

## SQLite is the better fit when

- the database belongs to one application instance;
- deployment should be a single file;
- the workload is local, embedded, test, mobile, desktop, or read-mostly;
- operational simplicity matters more than high write concurrency.

## PostgreSQL is the better fit when

- many users or services write at the same time;
- row-level locking and stronger isolation are required;
- authentication, network access, auditing, replication, and backups matter;
- the application needs richer SQL, indexes, JSON, full text, or extensions.

## Main takeaway

SQLite wins by removing infrastructure. PostgreSQL wins by centralizing
coordination in a server. The design choice mostly depends on write concurrency:
one local writer points toward SQLite; many coordinated writers point toward
PostgreSQL.
