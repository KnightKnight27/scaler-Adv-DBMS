## Findings and Comparison

From the SQLite experiments, the database uses a page size of 4096 bytes and a total of 3 pages, which results in a file size of about 12 KB. This shows how compact SQLite is for small datasets. The entire database is stored as a single file, and even metadata is minimal.

When running queries, the execution time was around 0.003s without mmap and slightly improved to around 0.002s with mmap enabled. mmap (memory-mapped I/O — accessing file data directly through memory instead of traditional read/write calls) did not show a major improvement here because the dataset is very small. However, it still demonstrates that mmap reduces overhead slightly by avoiding extra copying between kernel space and user space.

In contrast, PostgreSQL shows a completely different behavior. Even with the same dataset, the database size is about 8078 KB (~8 MB). This is significantly larger than SQLite. The reason is that PostgreSQL is a server-based system and maintains additional internal structures such as WAL (Write-Ahead Logging — a system that logs changes before applying them for safety), system catalogs, and background processes.

Query execution time in PostgreSQL was around 0.011–0.012s, which is noticeably slower than SQLite for this small dataset. This overhead comes from the client-server architecture, process management, and additional safety mechanisms.

Comparing both systems:

SQLite is extremely lightweight, with minimal storage overhead and very fast query execution for small datasets. It works directly on a file and avoids the complexity of a server. Features like mmap can slightly improve performance, especially for larger or repeated reads.

PostgreSQL, on the other hand, is much heavier. It consumes more storage and has slower query execution for simple workloads, but this is because it provides stronger guarantees like concurrency control, crash recovery, and scalability. It is designed for multi-user environments and large-scale systems.

From this experiment, it is clear that SQLite is more efficient for small, local, or embedded use cases, while PostgreSQL trades performance and storage for robustness, scalability, and advanced database features.