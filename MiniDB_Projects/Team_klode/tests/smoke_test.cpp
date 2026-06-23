#include "minidb.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const std::string dir = "test_data_smoke";
    std::filesystem::remove_all(dir);

    minidb::MiniDB db(dir);
    db.execute("CREATE TABLE users (id, name, age)");
    db.execute("CREATE TABLE orders (id, user_id, amount)");
    db.execute("INSERT INTO users VALUES (1, Ada, 31)");
    db.execute("INSERT INTO users VALUES (2, Grace, 28)");
    db.execute("INSERT INTO orders VALUES (10, 1, 200)");

    auto indexed = db.execute("SELECT name FROM users WHERE id = 1");
    assert(indexed.rows.size() == 1);
    assert(indexed.rows[0][0] == "Ada");
    assert(indexed.message.find("index scan") != std::string::npos);
    assert(indexed.message.find("selectivity=") != std::string::npos);

    auto indexDemo = db.execute("INDEX_DEMO users");
    assert(indexDemo.rows.size() == 2);
    assert(indexDemo.rows[0][0] == "1");
    assert(indexDemo.rows[1][0] == "2");

    auto joined = db.execute("SELECT name,amount FROM users JOIN orders ON users.id = orders.user_id WHERE amount > 100");
    assert(joined.rows.size() == 1);
    assert(joined.rows[0][0] == "Ada");
    assert(joined.message.find("join order=") != std::string::npos);
    assert(joined.message.find("selectivity users=") != std::string::npos);

    auto lockDemo = db.execute("LOCK_DEMO");
    assert(lockDemo.rows.size() == 5);
    assert(lockDemo.rows.back()[1] == "detected");

    auto perfDemo = db.execute("PERF_DEMO");
    assert(perfDemo.rows.size() == 2);
    assert(perfDemo.rows[0][2] == perfDemo.rows[1][2]);

    auto storageDemo = db.execute("STORAGE_DEMO");
    assert(storageDemo.rows.size() == 2);
    assert(storageDemo.message.find("heap files") != std::string::npos);

    db.execute("DELETE FROM users WHERE id = 2");
    auto deleted = db.execute("SELECT * FROM users WHERE id = 2");
    assert(deleted.rows.empty());

    db.execute("BEGIN");
    db.execute("INSERT INTO users VALUES (4, Barbara, 37)");
    db.execute("ROLLBACK");
    auto rolledBack = db.execute("SELECT * FROM users WHERE id = 4");
    assert(rolledBack.rows.empty());

    db.execute("BEGIN");
    db.execute("INSERT INTO users VALUES (5, Duplicate, 20)");
    bool duplicateRejected = false;
    try {
        db.execute("INSERT INTO users VALUES (5, DuplicateAgain, 21)");
    } catch (const std::runtime_error&) {
        duplicateRejected = true;
    }
    assert(duplicateRejected);
    db.execute("ROLLBACK");

    minidb::MiniDB recovered(dir);
    auto afterRecovery = recovered.execute("SELECT name FROM users WHERE id = 1");
    assert(afterRecovery.rows.size() == 1);
    assert(afterRecovery.rows[0][0] == "Ada");

    const std::string crashDir = "test_data_wal_replay";
    std::filesystem::remove_all(crashDir);
    std::filesystem::create_directories(crashDir);
    {
        std::ofstream wal(crashDir + "/minidb.wal");
        wal << "CREATE|users|id,name,age\n";
        wal << "BEGIN|99\n";
        wal << "INSERT|99|users|7,Dennis,52\n";
        wal << "COMMIT|99\n";
        wal << "BEGIN|100\n";
        wal << "INSERT|100|users|8,Uncommitted,1\n";
    }
    minidb::MiniDB crashRecovered(crashDir);
    auto committed = crashRecovered.execute("SELECT name FROM users WHERE id = 7");
    assert(committed.rows.size() == 1);
    assert(committed.rows[0][0] == "Dennis");
    auto uncommitted = crashRecovered.execute("SELECT * FROM users WHERE id = 8");
    assert(uncommitted.rows.empty());

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(crashDir);
    std::cout << "smoke test passed\n";
    return 0;
}
