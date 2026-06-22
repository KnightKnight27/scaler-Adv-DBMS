// Lab 2 — minimal driver for the mmap-vs-read strace experiment.
//
// Sets PRAGMA mmap_size to the value given on the command line, disables the
// page cache (so I/O actually hits the file), then runs one query. Trace it:
//
//   g++ -O2 -o query query.cpp -lsqlite3
//   strace -e trace=openat,pread64,read,mmap ./query 0          2> strace_nommap.txt
//   strace -e trace=openat,pread64,read,mmap ./query 268435456  2> strace_mmap.txt
#include <sqlite3.h>
#include <iostream>
#include <string>
#include <cstdlib>

static void exec(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "exec failed [" << sql << "]: " << (err ? err : "?") << "\n";
        sqlite3_free(err);
    }
}

int main(int argc, char** argv) {
    long long mmap_size = (argc > 1) ? std::atoll(argv[1]) : 0;

    sqlite3* db = nullptr;
    if (sqlite3_open("students.db", &db) != SQLITE_OK) {
        std::cerr << "open failed: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }

    exec(db, "PRAGMA mmap_size=" + std::to_string(mmap_size) + ";");
    exec(db, "PRAGMA cache_size=0;");   // force real I/O instead of serving from cache

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT count(*) FROM students;", -1, &stmt, nullptr);
    long long rows = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : -1;
    sqlite3_finalize(stmt);

    // Report the effective mmap_size back.
    sqlite3_prepare_v2(db, "PRAGMA mmap_size;", -1, &stmt, nullptr);
    long long eff = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : -1;
    sqlite3_finalize(stmt);

    std::cout << "rows: " << rows << "  mmap_size: " << eff << "\n";
    sqlite3_close(db);
    return 0;
}
