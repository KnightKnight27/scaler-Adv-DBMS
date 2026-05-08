# Lab 2 — Assignment Specification

**Course:** Advanced DBMS (Scaler)  
**Author:** Anushka Jain | 24BCS10193

---

## Submission Guidelines

All submissions must be made via the GitHub repository shared with the class.

Create a Pull Request (PR) with:
- Your Roll Number
- Your Name

Instructions are also shared on the WhatsApp (Scaler) channel.

---

## Lab Tasks

### 1. SQLite3 Exploration

Install SQLite3 and perform the following:

- Use any sample database
- Run `ls -lh` — observe file sizes
- Use `PRAGMA` commands to find:
  - Page size
  - Page count
- Experiment with `mmap_size`:
  - Try changing it and observe behavior
- Time queries: `time SELECT * FROM users;`
  - Compare execution time with mmap vs without mmap
- Use commands like: `ps aux | grep sqlite`

### 2. PostgreSQL (PSQL) Setup

Install PostgreSQL. Perform similar experiments:

- Page size
- Page count
- Query execution time

### 3. Comparison Report

Create a `.md` (Markdown) file. Include comparison between SQLite3 vs PostgreSQL:

- Page size
- Page count
- Query performance
- mmap impact

> **Do NOT submit screenshots. Only submit the Markdown file.**

---

## Submission Format

- File: `README.md` (or any `.md` file)
- Content: Observations, commands used, comparison analysis
