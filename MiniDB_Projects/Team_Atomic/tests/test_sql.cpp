// End-to-end SQL: CREATE / INSERT / SELECT (WHERE, JOIN, COUNT) / DELETE,
// plus persistence across reopening the database.
#include <cassert>
#include <cstdio>
#include <iostream>
#include "engine/database.h"

using namespace minidb;

static int RowCount(Database& db, const std::string& sql) {
  return db.Execute(sql).affected;
}

int main() {
  std::remove("t_sql.db");
  std::remove("t_sql.catalog");

  {
    Database db("t_sql");
    db.Execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR, age INTEGER)");
    db.Execute("CREATE TABLE orders (oid INTEGER PRIMARY KEY, uid INTEGER, amount INTEGER)");

    db.Execute("INSERT INTO users VALUES (1, 'alice', 30)");
    db.Execute("INSERT INTO users VALUES (2, 'bob', 25)");
    db.Execute("INSERT INTO users VALUES (3, 'carol', 40)");
    db.Execute("INSERT INTO orders VALUES (10, 1, 100)");
    db.Execute("INSERT INTO orders VALUES (11, 1, 250)");
    db.Execute("INSERT INTO orders VALUES (12, 3, 75)");

    // Duplicate PK rejected.
    bool threw = false;
    try { db.Execute("INSERT INTO users VALUES (1, 'dup', 1)"); }
    catch (const DBError&) { threw = true; }
    assert(threw);

    // SELECT *
    auto all = db.Execute("SELECT * FROM users");
    assert(all.rows.size() == 3);

    // WHERE
    auto w = db.Execute("SELECT name FROM users WHERE age > 28");
    assert(w.rows.size() == 2);  // alice, carol

    // WHERE with AND
    auto w2 = db.Execute("SELECT id FROM users WHERE age > 20 AND age < 35");
    assert(w2.rows.size() == 2);  // alice(30), bob(25)

    // COUNT(*)
    auto c = db.Execute("SELECT COUNT(*) FROM users");
    assert(c.rows.size() == 1 && c.rows[0][0].i == 3);

    // JOIN users x orders ON users.id = orders.uid
    auto j = db.Execute(
        "SELECT users.name, orders.amount FROM users "
        "JOIN orders ON users.id = orders.uid");
    assert(j.rows.size() == 3);  // alice(2) + carol(1)

    // JOIN + WHERE
    auto jw = db.Execute(
        "SELECT users.name, orders.amount FROM users "
        "JOIN orders ON users.id = orders.uid WHERE orders.amount > 90");
    assert(jw.rows.size() == 2);  // 100, 250

    // DELETE
    int del = RowCount(db, "DELETE FROM users WHERE id = 2");
    assert(del == 1);
    assert(db.Execute("SELECT * FROM users").rows.size() == 2);
    // Deleted PK can be reinserted (index entry removed).
    db.Execute("INSERT INTO users VALUES (2, 'bob2', 99)");
    assert(db.Execute("SELECT * FROM users").rows.size() == 3);
    std::cout << "[sql] CRUD + WHERE + AND + JOIN + COUNT + DELETE OK\n";
  }

  // Reopen: catalog + heap + index must persist.
  {
    Database db("t_sql");
    auto all = db.Execute("SELECT * FROM users");
    assert(all.rows.size() == 3);
    auto j = db.Execute(
        "SELECT users.name, orders.amount FROM users "
        "JOIN orders ON users.id = orders.uid");
    assert(j.rows.size() == 3);
    std::cout << "[sql] persistence across reopen OK\n";
  }

  std::remove("t_sql.db");
  std::remove("t_sql.catalog");
  std::cout << "[sql] OK\n";
  return 0;
}
