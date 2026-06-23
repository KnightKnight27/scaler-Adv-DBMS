#include <iostream>
#include <sqlite3.h>
#include <string>

// Helper to print query results or retrieve integer values
long long get_pragma_value(sqlite3* db, const std::string& pragma_query) {
    sqlite3_stmt* stmt;
    long long value = -1;
    if (sqlite3_prepare_v2(db, pragma_query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            value = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to prepare pragma statement: " << pragma_query << "\n";
    }
    return value;
}

int main() {
    sqlite3* db;
    std::string db_path = "students.db";

    std::cout << "--- Opening Database Connection (" << db_path << ") ---\n";
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }
    std::cout << "Successfully opened " << db_path << " (In-process library call)\n\n";

    // 1. Inspect Default Storage Settings
    std::cout << "=== Part 1: Inspecting Storage Internals ===\n";
    long long default_page_size = get_pragma_value(db, "PRAGMA page_size;");
    long long default_page_count = get_pragma_value(db, "PRAGMA page_count;");
    long long default_mmap_size = get_pragma_value(db, "PRAGMA mmap_size;");

    std::cout << "Default Page Size : " << default_page_size << " bytes\n";
    std::cout << "Current Page Count: " << default_page_count << "\n";
    std::cout << "Current Database Size: " << (default_page_size * default_page_count) << " bytes\n";
    std::cout << "Default mmap Size : " << default_mmap_size << " bytes\n\n";

    // 2. Configure Memory Map size (mmap)
    std::cout << "=== Part 2: Tuning mmap Size ===\n";
    std::cout << "Setting mmap_size = 268435456 (256 MB) to enable memory-mapped I/O...\n";
    sqlite3_exec(db, "PRAGMA mmap_size = 268435456;", nullptr, nullptr, nullptr);
    
    long long updated_mmap_size = get_pragma_value(db, "PRAGMA mmap_size;");
    std::cout << "Updated mmap Size: " << updated_mmap_size << " bytes\n\n";

    // 3. Create Schema and Populate Data
    std::cout << "=== Part 3: Creating Schema & Inserting Data ===\n";
    const char* create_table_sql = 
        "CREATE TABLE IF NOT EXISTS students ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "age INTEGER"
        ");";
    
    char* err_msg = nullptr;
    rc = sqlite3_exec(db, create_table_sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error creating table: " << err_msg << "\n";
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }
    std::cout << "Table 'students' created (if it did not exist).\n";

    // Insert sample records
    const char* insert_sql = 
        "INSERT INTO students (name, age) VALUES ('Alice', 20);"
        "INSERT INTO students (name, age) VALUES ('Bob', 22);"
        "INSERT INTO students (name, age) VALUES ('Charlie', 21);";
    
    rc = sqlite3_exec(db, insert_sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error inserting data: " << err_msg << "\n";
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }
    std::cout << "Inserted sample students: Alice (20), Bob (22), Charlie (21).\n\n";

    // 4. Query & Print Data
    std::cout << "=== Part 4: Querying Data ===\n";
    sqlite3_stmt* select_stmt;
    const char* select_sql = "SELECT id, name, age FROM students;";
    
    rc = sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, nullptr);
    if (rc == SQLITE_OK) {
        std::cout << "ID | Name    | Age\n";
        std::cout << "-------------------\n";
        while (sqlite3_step(select_stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(select_stmt, 0);
            const unsigned char* name = sqlite3_column_text(select_stmt, 1);
            int age = sqlite3_column_int(select_stmt, 2);
            std::cout << id << "  | " << name << "  | " << age << "\n";
        }
        sqlite3_finalize(select_stmt);
    } else {
        std::cerr << "Failed to query students.\n";
    }
    std::cout << "\n";

    // 5. Inspect Storage Settings After Mutations
    std::cout << "=== Part 5: Storage After Mutations ===\n";
    long long new_page_count = get_pragma_value(db, "PRAGMA page_count;");
    std::cout << "New Page Count: " << new_page_count << "\n";
    std::cout << "New Database Size: " << (default_page_size * new_page_count) << " bytes\n\n";

    // Close Database connection
    std::cout << "--- Closing Connection ---\n";
    sqlite3_close(db);
    std::cout << "Database connection closed.\n";

    return 0;
}
