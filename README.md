# Database File System Analysis

## Overview
This project explores the internal storage mechanisms of SQLite by generating a sample database and dumping its hexadecimal representation. It is designed to help engineers and database administrators understand B-tree page structures, file headers, and record formatting at the byte level.

## Project Files
- `movies_catalog.db` (The generated SQLite database binary)
- `movies_catalog.hex` (The raw hex dump of the database file)
- `build_movies.sql` (The initialization script for schema and data)

## Database Schema Overview
**Table:** `movies`
- `movie_id` (INTEGER PRIMARY KEY)
- `movie_title` (TEXT)
- `director_name` (TEXT)
- `release_year` (INTEGER)

**Indexes:**
- `idx_movies_year` (Secondary B-tree index on the `release_year` column)

## Build Instructions
To recreate the database and generate the hex dump from scratch, run the following commands in your terminal:

```bash
# 1. Generate the SQLite database from the SQL script
sqlite3 movies_catalog.db < build_movies.sql

# 2. Create a hex dump for byte-level analysis
xxd -g 1 -c 16 movies_catalog.db > movies_catalog.hex
