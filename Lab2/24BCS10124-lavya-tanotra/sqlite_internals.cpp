// Name: Lavya Tanotra
// Roll No: 24BCS10124
// Lab 2: SQLite3 Internals — mmap, Page Size, PRAGMA & Library Architecture
//
// SQLite is an in-process library (links as libsqlite3.so), NOT a server process.
// The entire database lives in a single .db file divided into fixed-size pages.
//
// PRAGMA observations on students.db:
//   PRAGMA page_size;    → 4096  (matches OS page size; set at creation, immutable)
//   PRAGMA page_count;   → varies (total_file_size / page_size)
//   PRAGMA mmap_size;    → 0 by default; set to 268435456 (256 MB) to enable mmap I/O
//   PRAGMA journal_mode; → delete (default) or WAL for concurrent reads
//
// With mmap enabled, SQLite calls mmap() on the .db file so reads become
// memory accesses instead of read() syscalls → faster sequential access.
//
// strace comparison (strace -e trace=mmap,read sqlite3 students.db "SELECT count(*)..."):
//   mmap_size=0        → many read() syscalls per query
//   mmap_size=268435456 → one mmap() call, then direct memory access; fewer reads
//
// Architecture:
//   Your binary ──links──► libsqlite3.so ──reads/writes──► students.db (via OS syscalls)
//   No TCP socket, no separate process, no authentication handshake.
//   Concurrency is handled by file-level locks (WAL mode improves concurrent reads).

#include <iostream>
#include <sqlite3.h>
#include <cstring>

static int print_row(void* /*unused*/, int cols, char** values, char** names) {
    for (int i = 0; i < cols; ++i)
        std::cout << names[i] << " = " << (values[i] ? values[i] : "NULL") << "\n";
    std::cout << "---\n";
    return 0;
}

static void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, print_row, nullptr, &err) != SQLITE_OK) {
        std::cerr << "SQL error: " << err << "\n";
        sqlite3_free(err);
    }
}

int main() {
    sqlite3* db = nullptr;

    // sqlite3_open is a direct function call — no network, no fork
    if (sqlite3_open("students.db", &db) != SQLITE_OK) {
        std::cerr << "Cannot open DB: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }

    // Introspect storage layout via PRAGMA
    std::cout << "=== Storage internals via PRAGMA ===\n";
    exec(db, "PRAGMA page_size;");      // 4096 — one page = one disk I/O unit
    exec(db, "PRAGMA page_count;");     // current page count
    exec(db, "PRAGMA journal_mode;");   // WAL vs DELETE (rollback journal)
    exec(db, "PRAGMA cache_size;");     // in-memory page cache size

    // Enable mmap for faster sequential reads
    exec(db, "PRAGMA mmap_size = 268435456;");
    std::cout << "mmap_size after setting:\n";
    exec(db, "PRAGMA mmap_size;");      // should now return 268435456

    // Create and query a simple table to show in-process execution
    exec(db, "CREATE TABLE IF NOT EXISTS students "
             "(id INTEGER PRIMARY KEY, name TEXT, gpa REAL);");
    exec(db, "INSERT OR IGNORE INTO students VALUES (1,'Alice',3.8);");
    exec(db, "INSERT OR IGNORE INTO students VALUES (2,'Bob',2.9);");
    exec(db, "INSERT OR IGNORE INTO students VALUES (3,'Carol',3.5);");

    std::cout << "\n=== Query result ===\n";
    exec(db, "SELECT * FROM students WHERE gpa > 3.0 ORDER BY gpa DESC;");

    exec(db, "PRAGMA integrity_check;"); // validate all pages

    sqlite3_close(db);
    return 0;
}

// Compile: g++ -std=c++17 -o sqlite_internals sqlite_internals.cpp -lsqlite3
// Run:     ./sqlite_internals
