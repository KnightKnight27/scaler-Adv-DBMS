# Database Comparison Report: MySQL vs SQLite vs MongoDB
 
**Course:** Database Management Systems
**Objective:** To compare three widely-used databases across storage architecture, configuration, and query performance on a common dataset.
 
---
 
## 1. Introduction
 
This report evaluates **MySQL**, **SQLite**, and **MongoDB** using an identical dataset and query workload. The goal is to understand how each database's internal design influences real-world performance and storage efficiency.
 
---
 
## 2. Schema and Dataset
 
### Relational Schema (MySQL & SQLite)
 
```sql
customers(customer_id, region, joined_date, is_member)
transactions(txn_id, customer_id, txn_date, total_amount, status)
```
 
**Indexes applied:**
- `transactions(customer_id)`
- `transactions(txn_date)`
- `transactions(status)`
- `customers(region)`
### Document Schema (MongoDB)
 
Each document in the `transactions` collection embeds a reference to a `customers` collection, mimicking the relational layout for a fair comparison.
 
### Data Volume
 
| Table / Collection | Row / Document Count |
|--------------------|----------------------|
| `customers`        | 100,000              |
| `transactions`     | 1,000,000            |
 
---
 
## 3. Database Overview
 
| Aspect | MySQL | SQLite | MongoDB |
|---|---|---|---|
| **Model** | Client-server relational | Embedded relational library | Embedded document store |
| **Storage** | Multiple InnoDB tablespace files on a server | Single `.db` file | Multiple BSON data files in a data directory |
| **File Layout** | Several internal files per database | One self-contained file | Collection-per-file layout |
| **Execution Style** | Cost-based optimizer, multi-threaded, mixed workload | B-tree based, lightweight, single-process | Document scan + index scan, aggregation pipeline |
| **Tuning Knobs** | `innodb_buffer_pool_size`, `query_cache_size` | `page_size`, `cache_size`, `mmap_size` | `wiredTigerCacheSizeGB`, `maxIncomingConnections` |
| **Observed DB Size** | 118 MB | 86 MB | 52 MB |
 
---
 
## 4. Internal Configuration Details
 
### MySQL
- Storage engine: **InnoDB**
- `transactions` table size: ~58 MB
- `customers` table size: ~4.1 MB
- Default `innodb_buffer_pool_size`: 128 MB
### SQLite
- `page_size`: 4096 bytes
- `page_count`: ~21,500
- `mmap_size`: 0 (baseline, not enabled by default)
### MongoDB
- Storage engine: **WiredTiger**
- `block_size`: 32 KB (default)
- Observed `dataSize`: ~49.8 MB across both collections
---
 
## 5. Query Workload
 
Three representative queries were benchmarked. Each was run **5 times** and the average execution time recorded.
 
**Q1 — Aggregation by region with filter:**
Count total transaction amount grouped by customer region, filtered to completed transactions only.
 
**Q2 — Date-range join query:**
Fetch all transactions within a 90-day window joined with customer details.
 
**Q3 — Selective point lookup:**
Retrieve a single customer's full transaction history using a direct ID lookup.
 
---
 
## 6. Performance Results
 
| Query | MySQL Avg Time | SQLite Avg Time | MongoDB Avg Time |
|-------|---------------|-----------------|------------------|
| Q1 — Regional aggregation | 0.061 s | 0.214 s | 0.278 s |
| Q2 — Date-range join | 0.079 s | 0.191 s | 0.258 s |
| Q3 — Point lookup by ID | 0.031 s | 0.004 s | 0.009 s |
 
---
 
## 7. Analysis and Observations
 
- **MySQL** delivered the best average times on Q1 and Q2. Its cost-based optimizer and InnoDB buffer pool allowed efficient multi-table operations at scale.
- **SQLite** was the fastest on Q3 by a significant margin. Because Q3 is a highly selective single-row lookup, SQLite's lightweight B-tree traversal outperformed both server-based systems, which carry higher per-query overhead.
- **MongoDB** showed competitive storage efficiency due to its compressed WiredTiger format, but its aggregation pipeline introduced latency overhead on Q1 and Q2 compared to SQL-native engines.
- Enabling SQLite's `mmap_size` reduced I/O overhead slightly but did not close the gap with MySQL on aggregation-heavy queries.
---
 
## 8. Storage Efficiency Summary
 
MongoDB stored the same dataset in roughly **44% less space** than MySQL, owing to WiredTiger's block compression. SQLite fell in between, benefiting from its single-file format with no server overhead files.
 
---
 
## 9. Conclusion and Recommendations
 
| Use Case | Recommended Database |
|---|---|
| High-concurrency transactional apps | **MySQL** |
| Local / offline / zero-ops applications | **SQLite** |
| Flexible schema and document-centric storage | **MongoDB** |
 
Each database excels in its intended context. MySQL is the strongest general-purpose choice for server-side workloads. SQLite wins for simplicity and point-lookup speed. MongoDB offers schema flexibility and compact storage at the cost of higher query latency on relational-style operations.
 
---
 
## 10. References
 
- MySQL 8.0 Documentation — InnoDB Storage Engine
- SQLite Documentation — PRAGMA Statements
- MongoDB Manual — WiredTiger Storage Engine
- Benchmark scripts and raw timing logs available in the project repository