// Lab 2 — SQLite3 internals in C++ (uses libsqlite3 directly, in-process).
// Part 2 (PRAGMA introspection) + Part 3 (library-not-a-server) demonstration.
//
// Build:  g++ -O2 -o sqlite_internals sqlite_internals.cpp -lsqlite3
// Run:    ./sqlite_internals students.db
#include <sqlite3.h>
#include <iostream>
#include <string>
#include <unistd.h>   // getpid

// Run a PRAGMA / query and print "<label> -> <first column of first row>".
static void show(sqlite3* db, const std::string& sql, const std::string& label) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "prepare failed for [" << sql << "]: " << sqlite3_errmsg(db) << "\n";
        return;
    }
    std::cout << label;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(stmt, 0);
        std::cout << (txt ? reinterpret_cast<const char*>(txt) : "NULL");
    } else {
        std::cout << "(no row)";
    }
    std::cout << "\n";
    sqlite3_finalize(stmt);
}

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "students.db";

    std::cout << "sqlite3 library version : " << sqlite3_libversion() << "\n";
    std::cout << "application PID          : " << getpid()
              << "   (the DB engine runs INSIDE this process)\n\n";

    sqlite3* db = nullptr;
    if (sqlite3_open(path, &db) != SQLITE_OK) {           // in-process function call — no server, no socket
        std::cerr << "open failed: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }

    std::cout << "=== Part 2: storage internals via PRAGMA (" << path << ") ===\n";
    show(db, "PRAGMA page_size;",       "PRAGMA page_size        -> ");
    show(db, "PRAGMA page_count;",      "PRAGMA page_count       -> ");
    show(db, "PRAGMA freelist_count;",  "PRAGMA freelist_count   -> ");
    show(db, "PRAGMA mmap_size;",       "PRAGMA mmap_size        -> ");
    show(db, "PRAGMA journal_mode;",    "PRAGMA journal_mode     -> ");
    show(db, "PRAGMA cache_size;",      "PRAGMA cache_size       -> ");
    show(db, "PRAGMA encoding;",        "PRAGMA encoding         -> ");
    show(db, "PRAGMA schema_version;",  "PRAGMA schema_version   -> ");
    show(db, "PRAGMA integrity_check;", "PRAGMA integrity_check  -> ");

    // page_size * page_count should equal the on-disk file size.
    show(db, "SELECT (SELECT page_size FROM pragma_page_size) * "
             "(SELECT page_count FROM pragma_page_count);",
             "page_size * page_count  -> ");

    std::cout << "\n=== schema + data ===\n";
    show(db, "SELECT sql FROM sqlite_master WHERE name='students';", "table DDL          -> ");
    show(db, "SELECT count(*) FROM students;",                       "row count          -> ");

    sqlite3_close(db);                                    // in-process: just frees memory + closes the fd
    std::cout << "\nclosed — no IPC, no daemon involved; PID " << getpid()
              << " did everything itself.\n";
    return 0;
}
