#include <iostream>
#include <sqlite3.h>
#include <string>

// Callback function for SELECT queries
int callback(void* /* data */, int argc, char** argv, char** azColName) {
    for (int i = 0; i < argc; i++) {
        std::cout << azColName[i] << " = " << (argv[i] ? argv[i] : "NULL") << "\n";
    }
    std::cout << "---\n";
    return 0;
}

int main() {
    sqlite3* db;
    char* errMsg = nullptr;
    int rc;

    std::cout << "=== SQLite3 C++ Demo ===" << std::endl;
    std::cout << "SQLite Version: " << sqlite3_libversion() << std::endl << std::endl;

    // Open database
    rc = sqlite3_open("students.db", &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "✓ Database opened successfully" << std::endl << std::endl;

    // Query 1: Get page_size using PRAGMA
    std::cout << "1. PRAGMA page_size:" << std::endl;
    rc = sqlite3_exec(db, "PRAGMA page_size;", callback, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
    std::cout << std::endl;

    // Query 2: Get page_count
    std::cout << "2. PRAGMA page_count:" << std::endl;
    rc = sqlite3_exec(db, "PRAGMA page_count;", callback, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
    std::cout << std::endl;

    // Query 3: Get mmap_size
    std::cout << "3. PRAGMA mmap_size:" << std::endl;
    rc = sqlite3_exec(db, "PRAGMA mmap_size;", callback, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
    std::cout << std::endl;

    // Query 4: Select students with GPA > 3.5
    std::cout << "4. Students with GPA > 3.5:" << std::endl;
    rc = sqlite3_exec(db, 
        "SELECT name, gpa, major FROM students WHERE gpa > 3.5 ORDER BY gpa DESC;",
        callback, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
    std::cout << std::endl;

    // Query 5: Count students by major
    std::cout << "5. Students by major:" << std::endl;
    rc = sqlite3_exec(db,
        "SELECT major, COUNT(*) as count FROM students GROUP BY major ORDER BY count DESC;",
        callback, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
    std::cout << std::endl;

    // Close database
    sqlite3_close(db);
    std::cout << "✓ Database closed successfully" << std::endl;

    return 0;
}
