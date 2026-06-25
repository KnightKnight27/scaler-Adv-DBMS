// Lab 2: SQLite3 as an in-process library (not a server).
//
// Demonstrates that SQLite runs inside our own process: we link libsqlite3
// and call sqlite3_open/exec/close directly -- no TCP socket, no daemon.
// Also reads the storage-internal PRAGMAs (page_size, page_count, mmap_size).
//
// Build:  g++ -std=c++17 sqlite_demo.cpp -lsqlite3 -o sqlite_demo
// Run:    ./sqlite_demo
//
// Verify there is no server process backing it:
//   ps aux | grep sqlite      # nothing but our own process
//   ldd ./sqlite_demo         # libsqlite3.so.0 => ...

#include <sqlite3.h>
#include <iostream>
#include <string>

// Print every row of a query as "col=val  col=val ..."
static int print_row(void* /*ctx*/, int ncols, char** vals, char** names) {
    for (int i = 0; i < ncols; ++i)
        std::cout << names[i] << "=" << (vals[i] ? vals[i] : "NULL") << "  ";
    std::cout << "\n";
    return 0;
}

static void run(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    std::cout << "sqlite> " << sql << "\n";
    if (sqlite3_exec(db, sql.c_str(), print_row, nullptr, &err) != SQLITE_OK) {
        std::cerr << "  error: " << (err ? err : "?") << "\n";
        sqlite3_free(err);
    }
}

int main() {
    sqlite3* db = nullptr;
    if (sqlite3_open("students.db", &db) != SQLITE_OK) {
        std::cerr << "cannot open db: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }
    std::cout << "Opened students.db in-process via libsqlite3 "
              << sqlite3_libversion() << "\n\n";

    run(db, "CREATE TABLE IF NOT EXISTS students("
            "id INTEGER PRIMARY KEY, name TEXT, age INTEGER, gpa REAL);");
    run(db, "INSERT INTO students(name, age, gpa) VALUES "
            "('Alice',22,3.8),('Bob',25,2.9),('Carol',21,3.5),('Dave',30,3.1);");

    std::cout << "\n--- storage internals ---\n";
    run(db, "PRAGMA page_size;");
    run(db, "PRAGMA page_count;");
    run(db, "PRAGMA mmap_size;");
    run(db, "PRAGMA mmap_size = 268435456;");  // enable 256 MB mmap
    run(db, "PRAGMA mmap_size;");
    run(db, "PRAGMA journal_mode;");

    std::cout << "\n--- query ---\n";
    run(db, "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC;");

    sqlite3_close(db);
    return 0;
}
