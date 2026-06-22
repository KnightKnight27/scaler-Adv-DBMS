#include <iostream>
#include "sqlite3.h"

static int callback(void *data, int argc, char **argv, char **azColName) {
    for (int i = 0; i < argc; i++) {
        std::cout << azColName[i] << " = " << (argv[i] ? argv[i] : "NULL") << "\n";
    }
    std::cout << "\n";
    return 0;
}

int main() {
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;

    rc = sqlite3_open("students.db", &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }
    std::cout << "Opened database successfully\n\n";

    std::cout << "--- PRAGMA values ---\n";
    sqlite3_exec(db, "PRAGMA page_size;", callback, 0, &zErrMsg);
    sqlite3_exec(db, "PRAGMA page_count;", callback, 0, &zErrMsg);
    sqlite3_exec(db, "PRAGMA journal_mode;", callback, 0, &zErrMsg);

    std::cout << "--- Querying students table ---\n";
    rc = sqlite3_exec(db, "SELECT * FROM students;", callback, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << zErrMsg << "\n";
        sqlite3_free(zErrMsg);
    }

    sqlite3_close(db);
    return 0;
}
