# Lab Submission: SQLite3 & PostgreSQL Exploration

## Submission Guidelines
* **Platform:** All submissions must be made via the GitHub repository shared with the class.
* **Process:** Create a Pull Request (PR) containing:
    * Your Role Number
    * Your Name
* **Updates:** Additional instructions will be shared on the WhatsApp (Scalar) channel.

---

## Lab Tasks

### 1. SQLite3 Exploration
1.  **Installation:** Install SQLite3 on your system.
2.  **Database Setup:** Use any sample database.
3.  **File System Inspection:**
    * Run `ls -lh` to observe and record file sizes.
4.  **Database Metadata:** Use `PRAGMA` commands to find:
    * `page_size`
    * `page_count`
5.  **Memory Mapping (mmap) Experiment:**
    * Experiment with `mmap_size`. 
    * Modify settings and observe changes in behavior.
6.  **Performance Benchmarking:**
    * Time your queries using: `time SELECT * FROM users;`
    * Compare execution times:
        * With `mmap` enabled.
        * Without `mmap`.
7.  **Process Monitoring:**
    * Monitor the process using: `ps aux | grep sqlite`

### 2. PostgreSQL (PSQL) Setup
1.  **Installation:** Install PostgreSQL.
2.  **Comparative Analysis:** Perform similar experiments as conducted with SQLite3:
    * Identify Page Size.
    * Identify Page Count.
    * Measure Query execution time.

### 3. Comparison Report
Create a Markdown (`.md`) file documenting the following:
* **SQLite3 vs PostgreSQL Comparison:**
    * Page Size
    * Page Count
    * Query Performance
    * Impact of `mmap`
* **Important Notes:**
    * 🚫 **Do NOT** submit screenshots.
    * ✅ Only submit the Markdown file.

---

## Submission Format
* **File Name:** `README.md` (or any appropriate `.md` file).
* **Required Content:**
    * Detailed Observations.
    * List of Commands used.
    * Comparison analysis.
