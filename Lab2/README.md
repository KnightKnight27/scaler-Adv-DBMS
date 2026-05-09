# Lab 2: Comparing PostgreSQL (PSQL) and SQLite

## Overview

This lab explores the differences between two popular relational database management systems:
- **PostgreSQL (PSQL)** – a powerful, open-source, server-based RDBMS
- **SQLite** – a lightweight, file-based, serverless RDBMS

We compare them across setup, performance, SQL syntax, and use cases using a single shell script.

---

## Table of Contents

1. [Setup & Installation](#setup--installation)
2. [Key Differences](#key-differences)
3. [Shell Script Description](#shell-script-description)
4. [How to Run](#how-to-run)
5. [Observations & Results](#observations--results)
6. [Conclusion](#conclusion)

---

## Setup & Installation

### PostgreSQL
```bash
sudo apt update && sudo apt install postgresql postgresql-contrib
sudo service postgresql start
sudo -u postgres psql
```

### SQLite
```bash
sudo apt install sqlite3
sqlite3 mydb.db
```

---

## Key Differences

| Feature              | PostgreSQL (PSQL)               | SQLite                          |
|----------------------|----------------------------------|---------------------------------|
| Architecture         | Client-Server                   | Serverless (file-based)         |
| Setup Complexity     | Requires installation & config  | Zero configuration              |
| Concurrency          | High (MVCC support)             | Limited (writer locks whole DB) |
| Data Types           | Rich (JSON, Arrays, UUID, etc.) | Dynamic typing (flexible)       |
| Storage              | Separate server process         | Single `.db` file               |
| Performance (large)  | Optimized for large datasets    | Best for small/medium datasets  |
| Use Case             | Production, multi-user apps     | Embedded, local, mobile apps    |
| Authentication       | Role-based access control       | File system permissions only    |
| Transactions         | Full ACID compliance            | Full ACID compliance            |

---

## Shell Script Description

### `comparison.sh`

A single script divided into 3 sections:

| Section | What it does |
|---------|-------------|
| **Section 1: Setup** | Creates a DB and `students` table in both PSQL and SQLite, inserts 5 records, and queries them |
| **Section 2: Performance** | Benchmarks bulk insert (1000 rows) and AVG query time for both databases using `time` |
| **Section 3: Features** | Demonstrates PSQL-native JSONB and ARRAY types vs SQLite's TEXT-based workarounds |

---

## How to Run

> Make sure PostgreSQL is running and SQLite3 is installed before executing.

```bash
chmod +x lab2_comparison.sh
./lab2_comparison.sh
```

---

## Observations & Results

### Setup
- SQLite required **zero configuration** — just run the script.
- PostgreSQL needed a running service and user credentials.

### Performance (1000 rows)

| Operation   | PostgreSQL | SQLite |
|-------------|------------|--------|
| Bulk Insert | ~0.8s      | ~0.3s  |
| AVG Query   | ~0.07s     | ~0.04s |

> *Results may vary based on hardware and system load.*

### Features
- **PostgreSQL** supports native `JSONB`, `ARRAY`, `UUID`, and more out of the box.
- **SQLite** uses TEXT-based workarounds (e.g., `json_extract()`, CSV strings) for complex types.

---

## Conclusion

| Scenario                     | Recommended DB |
|------------------------------|----------------|
| Small local app / prototype  | SQLite         |
| Mobile / embedded app        | SQLite         |
| Production web application   | PostgreSQL     |
| Multi-user concurrent access | PostgreSQL     |
| Complex queries / data types | PostgreSQL     |

**SQLite** is ideal for simplicity and portability. **PostgreSQL** is the go-to for scalability, security, and advanced features in production environments.

---

## Author

Submitted as part of Lab 2 – Database Systems Lab  
Date: May 2026