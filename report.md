# Architectural Comparison: SQLite vs PostgreSQL Storage and Query Execution

**Author:** Praneeth Budati (Group B, College ID: 24BCS10081)
**Execution Date:** May 2026
**Test Environment:** macOS Sonoma, Terminal (zsh shell)
**Database Versions:** SQLite 3.45.x | PostgreSQL 16.x
**Dataset Overview:** A `staff_directory` relation was created in both database systems with approximately 250,000 entries. The schema consisted of six attributes: (`emp_id`, `full_name`, `contact_email`, `experience_yrs`, `branch_location`, `profile_notes`). Additional B-tree indexes were created on `branch_location` and `experience_yrs` to evaluate indexed query behavior. The `profile_notes` field contained padded text data (~150 bytes per row) to generate measurable storage and scan overhead.

> Note: All benchmarks and outputs were collected from local execution on the above environment. Supporting files and scripts are included within the repository.

---

## 1. Environment Setup and Schema Initialization

### 1.1 Table Definition

To maintain consistency across both systems, the exact same schema was initialized in SQLite and PostgreSQL.

```sql
CREATE TABLE staff_directory (
    emp_id          INTEGER PRIMARY KEY,
    full_name       TEXT NOT NULL,
    contact_email   TEXT NOT NULL,
    experience_yrs  INTEGER,
    branch_location TEXT,
    profile_notes   TEXT
);

CREATE INDEX idx_staff_branch ON staff_directory(branch_location);
CREATE INDEX idx_staff_exp    ON staff_directory(experience_yrs);
```

### 1.2 Database Verification

SQLite version verification:

```bash
sqlite3 --version
```

PostgreSQL version verification:

```bash
psql --version
```

Both systems were verified successfully before executing the workload benchmarks.
