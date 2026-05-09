# Database Storage and Memory Management Analysis

## Executive Summary
This analysis presents empirical findings on storage footprints, memory management strategies, and process architectures across four major database systems. The evaluation utilizes a standardized workload consisting of one hundred thousand inserted records to measure the operational mechanics of SQLite, PostgreSQL, MySQL, and MongoDB.

## Storage and Architecture Overview
The core differences in storage consumption and structural design dictate the specific utility of each system.

| Database System | Architecture Model | Default Page Size | Storage Footprint | Primary Caching Strategy |
| :--- | :--- | :--- | :--- | :--- |
| SQLite | Embedded Library | 4096 bytes | 2.0 Megabytes | Operating System Page Cache |
| PostgreSQL | Multi Process Server | 8192 bytes | 15.0 Megabytes | Managed Shared Buffers |
| MySQL | Multi Threaded Server | 16384 bytes | 12.0 Megabytes | InnoDB Buffer Pool |
| MongoDB | Multi Threaded Server | Variable Extents | 1.35 Megabytes | WiredTiger Cache and OS Cache |

## SQLite Findings
SQLite functions entirely as a linked library within the host application process, completely avoiding the overhead of background workers. The entire database resides within a single file. By matching its default page size to standard operating system memory pages, SQLite ensures optimal reading and writing efficiency. 

Enabling memory mapped input output allows SQLite to map the database file directly into the virtual address space of the calling process. Standard file input output involves a severe performance penalty known as the double copy, where data moves from physical storage into the kernel page cache and then gets copied again into the user buffer. Memory mapping circumvents this redundant copy, drastically improving read throughput. This configuration remains explicitly session scoped, automatically resetting on new connections to maintain stability in environments lacking reliable memory mapping.

## PostgreSQL Findings
PostgreSQL relies on a heavy client server model where a primary postmaster process forks independent backend processes for every connected client. The system maintains high concurrency via continuous background workers, specifically including the checkpointer, background writer, walwriter, and autovacuum launcher.

The expanded storage footprint directly stems from Multi Version Concurrency Control. PostgreSQL injects additional metadata and tuple headers alongside every record to manage simultaneous transaction visibility. Furthermore, it operates strictly on an internally managed shared buffers pool. Once disk pages load into this shared memory segment, subsequent queries fetch data directly from memory, leveraging a clock sweep algorithm for intelligent page eviction.

## MySQL Findings
MySQL utilizes the InnoDB storage engine within a single massive process that spawns lightweight threads for active connections. This design minimizes baseline memory consumption compared to full process forks while maintaining high throughput. 

Storage overhead sits noticeably lower than PostgreSQL. InnoDB optimizes disk seek times on traditional hardware by enforcing a substantial sixteen kilobyte default page size. Read and write operations flow exclusively through the InnoDB Buffer Pool, a specialized memory cache employing a modified least recently used algorithm to prevent sweeping table scans from discarding vital index pages.

## MongoDB Findings
MongoDB abandons rigid relational schemas in favor of a NoSQL document model managed by the WiredTiger engine. The data footprint is aggressively minimized down to roughly one megabyte through Snappy block compression, exploiting the repetitive nature of document keys to shrink storage drastically before disk persistence.

Memory management relies on a sophisticated dual cache architecture. Uncompressed data sits within the WiredTiger internal cache, while compressed data occupies the operating system filesystem cache. This division carefully balances central processing unit decompression cycles against raw input output latency.

## Performance and Timing Analysis
Timing experiments revealed the exact operational latency associated with these distinct caching mechanisms across our dataset. 

| Database System | Query Executed | Observed Execution Time | Caching Context |
| :--- | :--- | :--- | :--- |
| SQLite | Full Table Select | 820 milliseconds | Uncached disk read via standard process |
| PostgreSQL | Table Count | 36 milliseconds | Initial disk read to shared buffers |
| PostgreSQL | Table Count | 10 milliseconds | Cached read from shared buffers |
| PostgreSQL | Full Table Select | 54 milliseconds | Network transfer of full rows |

## Optimal Deployment Scenarios
The architectural trade offs directly define the environments where each system operates at peak efficiency.

| Database System | Optimal When | Suboptimal When |
| :--- | :--- | :--- |
| SQLite | Single active writer, minimal latency required, zero server setup desired | High concurrent writes, dataset exceeds operating system cache |
| PostgreSQL | High concurrent multi user access, complex query planning, strict crash recovery | Resource constrained embedded systems, simple local applications |
| MySQL | Heavy read web applications, highly threaded environments | Systems requiring zero configuration overhead |
| MongoDB | Unstructured data iteration, extreme storage compression required | Strict relational schema enforcement is necessary |
