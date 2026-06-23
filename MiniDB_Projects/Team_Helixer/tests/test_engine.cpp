// End-to-end SQL test: CREATE/INSERT/SELECT (index point, seq+filter), JOIN,
// and DELETE, all driven through Database::execute (parser -> optimizer ->
// executor -> storage). Also checks the optimizer picks an index point scan.
#include <cassert>
#include <cstdio>
#include <iostream>
#include "engine/database.h"

using namespace minidb;

static void cleanup(const std::string &b) {
    std::remove((b + ".db").c_str());
    std::remove((b + ".wal").c_str());
}

int main() {
    const std::string base = "test_eng";
    cleanup(base);
    {
        Database db(base);
        assert(db.execute("CREATE TABLE users (id INT, name VARCHAR, age INT, PRIMARY KEY (id))").ok);
        assert(db.execute("CREATE TABLE orders (oid INT, uid INT, amt INT, PRIMARY KEY (oid))").ok);

        for (int i = 1; i <= 5; ++i)
            assert(db.execute("INSERT INTO users VALUES (" + std::to_string(i) +
                              ", 'user" + std::to_string(i) + "', " + std::to_string(20 + i * 5) + ")").ok);
        // Duplicate primary key is rejected.
        assert(!db.execute("INSERT INTO users VALUES (1, 'dup', 99)").ok);

        db.execute("INSERT INTO orders VALUES (101, 1, 500)");
        db.execute("INSERT INTO orders VALUES (102, 1, 250)");
        db.execute("INSERT INTO orders VALUES (103, 3, 900)");

        // Index point lookup: WHERE id = 3 should use INDEX_POINT.
        auto r1 = db.execute("SELECT id, name FROM users WHERE id = 3");
        assert(r1.ok && r1.rows.size() == 1 && r1.rows[0][1].as_string() == "user3");
        assert(r1.message.find("INDEX_POINT") != std::string::npos);

        // Sequential scan + filter: WHERE age > 30.
        auto r2 = db.execute("SELECT name, age FROM users WHERE age > 30");
        // ages are 25,30,35,40,45 -> >30 keeps 35,40,45 = 3 rows
        assert(r2.ok && r2.rows.size() == 3);
        assert(r2.message.find("SEQ_SCAN") != std::string::npos);

        // Join users x orders on users.id = orders.uid.
        auto r3 = db.execute(
            "SELECT users.name, orders.amt FROM users JOIN orders ON users.id = orders.uid");
        assert(r3.ok && r3.rows.size() == 3); // 2 orders for user1 + 1 for user3
        assert(r3.message.find("NESTED_LOOP_JOIN") != std::string::npos);

        // Delete and verify.
        assert(db.execute("DELETE FROM users WHERE id = 2").ok);
        auto r4 = db.execute("SELECT id FROM users WHERE id = 2");
        assert(r4.ok && r4.rows.empty());
        auto r5 = db.execute("SELECT id FROM users");
        assert(r5.rows.size() == 4);
    }
    cleanup(base);
    std::cout << "[OK] engine: CREATE/INSERT/SELECT(index+seq)/JOIN/DELETE + "
                 "optimizer plan selection all verified" << std::endl;
    return 0;
}
