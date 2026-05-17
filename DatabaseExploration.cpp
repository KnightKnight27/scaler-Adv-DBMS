#include <iostream>
#include <chrono>
#include <string>
#include <vector>

// Note: To compile and run this code, you will need the C/C++ client libraries for each database.
//
// SQLite: Requires sqlite3 (e.g., -lsqlite3)
// #include <sqlite3.h>
//
// PostgreSQL: Requires libpq (e.g., -lpq)
// #include <libpq-fe.h>
//
// MySQL: Requires libmysqlclient (e.g., -lmysqlclient)
// #include <mysql/mysql.h>
//
// MongoDB: Requires mongo-cxx-driver (e.g., -lmongocxx -lbsoncxx)
// #include <bsoncxx/builder/stream/document.hpp>
// #include <mongocxx/client.hpp>
// #include <mongocxx/instance.hpp>
// #include <mongocxx/uri.hpp>

using namespace std;
using namespace std::chrono;

const int RECORD_COUNT = 100000;

void exploreSQLite() {
    cout << "--- SQLite Exploration ---" << endl;
    /*
    sqlite3* db;
    if (sqlite3_open("advDbLab.db", &db)) {
        cerr << "Can't open database: " << sqlite3_errmsg(db) << endl;
        return;
    }

    sqlite3_exec(db, "PRAGMA page_size = 4096;", 0, 0, 0);
    sqlite3_exec(db, "PRAGMA mmap_size = 268435456;", 0, 0, 0);
    sqlite3_exec(db, "DROP TABLE IF EXISTS users;", 0, 0, 0);
    sqlite3_exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER);", 0, 0, 0);

    cout << "Inserting " << RECORD_COUNT << " records into SQLite..." << endl;
    auto start = high_resolution_clock::now();

    sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "INSERT INTO users (name, age) VALUES (?, ?);", -1, &stmt, 0);

    for (int i = 1; i <= RECORD_COUNT; i++) {
        string name = "User" + to_string(i);
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, 20 + (i % 10));
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_exec(db, "COMMIT;", 0, 0, 0);
    sqlite3_finalize(stmt);

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(stop - start);
    cout << "SQLite: Inserted " << RECORD_COUNT << " records in " << duration.count() << " ms\n" << endl;

    sqlite3_close(db);
    */
    cout << "(Uncomment code and link sqlite3 to run)\n" << endl;
}

void explorePostgreSQL() {
    cout << "--- PostgreSQL Exploration ---" << endl;
    /*
    PGconn *conn = PQsetdbLogin("localhost", "5432", nullptr, nullptr, "oslab_pg", "postgres", "password");
    if (PQstatus(conn) != CONNECTION_OK) {
        cerr << "Connection to database failed: " << PQerrorMessage(conn) << endl;
        PQfinish(conn);
        return;
    }

    PQexec(conn, "DROP TABLE IF EXISTS users;");
    PQexec(conn, "CREATE TABLE users (id SERIAL PRIMARY KEY, name TEXT, age INTEGER);");

    cout << "Inserting " << RECORD_COUNT << " records into PostgreSQL..." << endl;
    auto start = high_resolution_clock::now();

    PQexec(conn, "BEGIN;");
    PQprepare(conn, "insert_user", "INSERT INTO users (name, age) VALUES ($1, $2)", 2, nullptr);

    for (int i = 1; i <= RECORD_COUNT; i++) {
        string name = "User" + to_string(i);
        string age = to_string(20 + (i % 10));
        const char *paramValues[2] = {name.c_str(), age.c_str()};
        PGresult *res = PQexecPrepared(conn, "insert_user", 2, paramValues, nullptr, nullptr, 0);
        PQclear(res);
    }
    PQexec(conn, "COMMIT;");

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(stop - start);
    cout << "PostgreSQL: Inserted " << RECORD_COUNT << " records in " << duration.count() << " ms\n" << endl;

    PQfinish(conn);
    */
    cout << "(Uncomment code and link libpq to run)\n" << endl;
}

void exploreMySQL() {
    cout << "--- MySQL (InnoDB) Exploration ---" << endl;
    /*
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        cerr << "mysql_init() failed" << endl;
        return;
    }

    if (mysql_real_connect(conn, "localhost", "root", "password", "oslab_mysql", 3306, NULL, 0) == NULL) {
        cerr << "mysql_real_connect() failed: " << mysql_error(conn) << endl;
        mysql_close(conn);
        return;
    }

    mysql_query(conn, "DROP TABLE IF EXISTS users;");
    mysql_query(conn, "CREATE TABLE users (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(255), age INT) ENGINE=InnoDB;");

    cout << "Inserting " << RECORD_COUNT << " records into MySQL..." << endl;
    auto start = high_resolution_clock::now();

    mysql_query(conn, "START TRANSACTION;");

    string bulk_query = "INSERT INTO users (name, age) VALUES ";
    for (int i = 1; i <= RECORD_COUNT; i++) {
        bulk_query += "('User" + to_string(i) + "', " + to_string(20 + (i % 10)) + ")";
        if (i % 10000 == 0 || i == RECORD_COUNT) {
            mysql_query(conn, bulk_query.c_str());
            bulk_query = "INSERT INTO users (name, age) VALUES ";
        } else {
            bulk_query += ", ";
        }
    }
    mysql_query(conn, "COMMIT;");

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(stop - start);
    cout << "MySQL: Inserted " << RECORD_COUNT << " records in " << duration.count() << " ms\n" << endl;

    mysql_close(conn);
    */
    cout << "(Uncomment code and link mysqlclient to run)\n" << endl;
}

void exploreMongoDB() {
    cout << "--- MongoDB (WiredTiger) Exploration ---" << endl;
    /*
    mongocxx::instance inst{}; // Initialize the driver
    mongocxx::client conn{mongocxx::uri{"mongodb://localhost:27017"}};
    
    auto db = conn["oslab_mongo"];
    auto collection = db["users"];
    collection.drop();

    cout << "Inserting " << RECORD_COUNT << " records into MongoDB..." << endl;
    auto start = high_resolution_clock::now();

    auto bulk = collection.create_bulk_write();
    for (int i = 1; i <= RECORD_COUNT; i++) {
        bsoncxx::builder::stream::document document{};
        document << "name" << "User" + to_string(i)
                 << "age" << 20 + (i % 10);
        
        bulk.append(mongocxx::model::insert_one{document.view()});
        
        if (i % 10000 == 0 || i == RECORD_COUNT) {
            bulk.execute();
            if (i != RECORD_COUNT) {
                bulk = collection.create_bulk_write();
            }
        }
    }

    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(stop - start);
    cout << "MongoDB: Inserted " << RECORD_COUNT << " records in " << duration.count() << " ms\n" << endl;
    */
    cout << "(Uncomment code and link mongocxx to run)\n" << endl;
}

int main() {
    cout << "Starting Database Storage and Memory Management Exploration...\n" << endl;
    
    exploreSQLite();
    explorePostgreSQL();
    exploreMySQL();
    exploreMongoDB();
    
    cout << "Exploration completed." << endl;
    return 0;
}
