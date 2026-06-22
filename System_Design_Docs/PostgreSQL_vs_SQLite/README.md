# PostgreSQL vs SQLite — Architecture Comparison

## 1. Problem Background
Both are SQL databases but built for different goals. PostgreSQL (from the Berkeley POSTGRES project) was made so many users can hit one big shared database at the same time. SQLite (by D. Richard Hipp, around 2000) wanted the opposite: no server, no setup, just a single file next to your app. So one solves "many clients, one server" and the other solves "I just need storage inside my app".

## 2. Architecture Overview
```
PostgreSQL (client-server)        SQLite (embedded)
 client --\                        +----------------+
 client ---+-> postmaster          |  your program  |
 client --/      |                  |  +----------+  |
            backend process         |  | sqlite   |  |
                 |                   |  +----+-----+  |
        shared buffers / WAL         +-------|--------+
                 |                           v
            data files               single .db file
```
Postgres runs as OS processes and talks over a socket. SQLite is a library inside your program, and the database is just one file.

## 3. Internal Design
Postgres stores tables as 8KB heap pages, caches hot pages in shared buffers, and writes a WAL before touching real files. It uses MVCC so readers and writers mostly don't block. SQLite uses pages too, but the whole DB (tables + indexes) lives in one file, and durability comes from a journal or WAL mode. Its concurrency is simple: basically one writer at a time. Both use B-trees for indexes.

## 4. Design Trade-Offs
Postgres is heavy: you run and tune a server, but you get real multi-user concurrency, replication, and rich types. SQLite drops all that for simplicity: nothing to install, super fast local reads, but heavy concurrent writes hurt because writers line up. So it's power and scale vs zero-config and tiny size. Neither is "better", they just sit at different points.

## 5. Experiments / Observations
I made a small `students.db` in SQLite and the whole database really is one file you can copy like any other. A read query returns instantly, no connection step. In Postgres the same query needs a running server and a connection first. The feel is different: SQLite is "open file, read", Postgres is "connect to service, ask service".

## Suggested Questions

**Why does SQLite work well for mobile apps?**
Phones don't want a background DB server burning battery and RAM, and apps mostly read their own local data. SQLite fits because it's just a library compiled into the app plus one file in the app's sandbox. No setup, no network, nothing to crash. Writes are usually single-user, so the one-writer limit barely matters. It's also tiny. That's why Android and iOS ship it as the default on-device store.

**Why is PostgreSQL preferred for large multi-user systems?**
Big systems have many clients writing at once and they can't block each other or corrupt data. Postgres was built for this. MVCC lets lots of readers and writers run together, so a long read doesn't freeze writers. It also has WAL crash recovery, replication, roles and permissions, and a smart planner for complex joins. SQLite's single-writer model becomes the bottleneck here, so for a shared backend Postgres fits better.

**What architectural decisions lead to these differences?**
It mostly comes down to client-server vs embedded, and everything follows. Because Postgres assumed many clients, it needed processes, shared memory, MVCC, and a network protocol. Because SQLite assumed one program owning one file, it could drop all that and just do direct file I/O with simple locking. Even the storage shows it: Postgres spreads data across files, SQLite keeps it in one portable file.

## 6. Key Learnings
The big takeaway is that "embedded vs client-server" decides almost everything else. Pick embedded and simple locking + one file make sense; pick client-server and you're pushed into processes, buffers, and MVCC. Also, calling one "better" misses the point. SQLite on a phone and Postgres on a busy server are the same idea applied to different problems. Match the database to your concurrency and deployment needs.
