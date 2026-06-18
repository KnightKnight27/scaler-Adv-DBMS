# scaler-Adv-DBMS

# sqlite

```bash
$ sqlite3 --version
3.47.2 2024-12-07 20:39:59 2aabe05e2e8cae4847a802ee2daddc1d7413d8fc560254d93ee3e72c1468alt1 (64-bit)

$ sqlite3 test.db
SQLite version 3.47.2 2024-12-07 20:39:59
Enter ".help" for usage hints.

sqlite> CREATE TABLE users(name TEXT);
INSERT INTO users VALUES ('Sushant');

sqlite> PRAGMA page_size;
4096

sqlite> PRAGMA page_count;
2

PRAGMA mmap_size;
0

sqlite> .quit
```

```bash
$ stat test.db
  File: test.db
  Size: 8192             Blocks: 16         IO Block: 4096   regular file
```

```txt
# DB size consistent with page_count * page_size, but diffrent block size 
```

```bash
Device: 0,44    Inode: 12731609    Links: 1
Access: (0644/-rw-r--r--)
Context: unconfined_u:object_r:user_home_t:s0
Access: 2026-05-09 23:07:21.143945482 +0530
Modify: 2026-05-09 23:06:49.385622881 +0530
Change: 2026-05-09 23:06:49.385622881 +0530
Birth: 2026-05-09 23:06:49.281621824 +0530
```

```bash
$ strace sqlite3 test.db 2> sqliteStrace.txt
```

```bash
$ ps aux | grep [s]qlite
```

```txt
# No output
```

# postgres

```bash
$ postgres --version
postgres (PostgreSQL) 17.9
```

```bash
$ sudo postgresql-setup --initdb
```

```bash
$ sudo systemctl start postgresql
```

```bash
$ ps aux | grep [p]ostgres
postgres  169499  0.0  0.0 294864  5312 ?        Ss   23:27   0:00 postgres: logger 
postgres  169500  0.0  0.0 441412  5752 ?        Ss   23:27   0:00 postgres: checkpointer 
postgres  169501  0.0  0.0 441436  6204 ?        Ss   23:27   0:00 postgres: background writer 
postgres  169503  0.0  0.0 441280  9800 ?        Ss   23:27   0:00 postgres: walwriter 
postgres  169504  0.0  0.0 442856  8604 ?        Ss   23:27   0:00 postgres: autovacuum launcher 
postgres  169505  0.0  0.0 442864  7720 ?        Ss   23:27   0:00 postgres: logical replication launcher 
```

```bash
$ sudo -u postgres psql

postgres=# SHOW block_size;
 block_size 
------------
 8192
(1 row)

postgres=# SHOW shared_buffers;
 shared_buffers 
----------------
 128MB
(1 row)

postgres=# SHOW data_directory;
   data_directory    
---------------------
 /var/lib/pgsql/data
(1 row)

postgres-# \q
```

```bash
$ sudo ls /var/lib/pgsql/data
base              pg_commit_ts   pg_logical    pg_serial     pg_subtrans  pg_wal                postmaster.opts
current_logfiles  pg_dynshmem    pg_multixact  pg_snapshots  pg_tblspc    pg_xact               postmaster.pid
global            pg_hba.conf    pg_notify     pg_stat       pg_twophase  postgresql.auto.conf
log               pg_ident.conf  pg_replslot   pg_stat_tmp   PG_VERSION   postgresql.conf
```

# Architectural observations

```txt
# SQLite:
# - Embedded database engine
# - Database stored as a normal local file
# - No persistent server process
# - Direct filesystem interaction visible via strace

# PostgreSQL:
# - Client/server DBMS architecture
# - Persistent background server processes
# - Dedicated internal storage hierarchy
# - Uses shared memory buffer pool
```