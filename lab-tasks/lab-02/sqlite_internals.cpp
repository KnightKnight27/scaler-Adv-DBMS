#include <iostream>
#include <sqlite3.h>
#include <string>

// Callback function to handle query results
static int callback(void *NotUsed, int argc, char **argv, char **azColName) {
    for (int i = 0; i < argc; i++) {
        std::cout << azColName[i] << " = " << (argv[i] ? argv[i] : "NULL") << std::endl;
    }
    std::cout << std::endl;
    return 0;
}

void execute_sql(sqlite3 *db, const std::string& sql) {
    char *zErrMsg = 0;
    int rc = sqlite3_exec(db, sql.c_str(), callback, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << zErrMsg << std::endl;
        sqlite3_free(zErrMsg);
    } else {
        std::cout << "Successfully executed: " << sql << std::endl;
    }
}

int main() {
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;

    // 1. Open Database
    std::cout << "Opening SQLite database..." << std::endl;
    rc = sqlite3_open("lab2_internals.db", &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return 0;
    }

    // 2. Setting PRAGMAs for internal configuration
    std::cout << "\n--- Configuring SQLite Internals ---" << std::endl;
    
    // Set page size to 8192 bytes (8KB). Must be done before creating tables.
    execute_sql(db, "PRAGMA page_size = 8192;");
    
    // Enable Memory-Mapped I/O for faster reading
    execute_sql(db, "PRAGMA mmap_size = 268435456;"); // 256 MB
    
    // Use Write-Ahead Logging for better concurrency
    execute_sql(db, "PRAGMA journal_mode = WAL;");

    // 3. Verify PRAGMAs
    std::cout << "\n--- Verifying Configuration ---" << std::endl;
    execute_sql(db, "PRAGMA page_size;");
    execute_sql(db, "PRAGMA mmap_size;");
    execute_sql(db, "PRAGMA journal_mode;");

    // 4. Create table and insert data to observe file size changes
    std::cout << "\n--- Inserting Data ---" << std::endl;
    std::string sql = "CREATE TABLE IF NOT EXISTS test_table ("
                      "ID INT PRIMARY KEY NOT NULL, "
                      "NAME TEXT NOT NULL, "
                      "DATA BLOB);";
    execute_sql(db, sql);

    sql = "INSERT INTO test_table (ID, NAME, DATA) VALUES (1, 'Test Record 1', randomblob(1000));";
    execute_sql(db, sql);
    sql = "INSERT INTO test_table (ID, NAME, DATA) VALUES (2, 'Test Record 2', randomblob(1000));";
    execute_sql(db, sql);

    // 5. Query data
    std::cout << "\n--- Querying Data ---" << std::endl;
    sql = "SELECT ID, NAME, length(DATA) as DATA_LEN FROM test_table;";
    execute_sql(db, sql);

    // 6. Close database
    std::cout << "Closing database..." << std::endl;
    sqlite3_close(db);

    std::cout << "Lab 2 complete. Check lab2_internals.db file size; it should be a multiple of the 8KB page size." << std::endl;

    return 0;
}
