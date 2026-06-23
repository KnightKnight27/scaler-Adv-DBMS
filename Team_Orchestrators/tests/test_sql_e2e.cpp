#include "minidb/database.hpp"
#include "test_util.hpp"
#include <cstdio>
using namespace minidb;

static void run_tests() {
  std::remove("t_e2e.data");
  std::remove("t_e2e.catalog");
  std::remove("t_e2e.wal");
  Database db("t_e2e");
  db.execute("CREATE TABLE u (id INT, name VARCHAR(20));");
  db.execute("INSERT INTO u VALUES (1, 'Alice');");
  db.execute("INSERT INTO u VALUES (2, 'Bob');");
  QueryResult r = db.execute("SELECT id, name FROM u;");
  CHECK(r.is_select);
  CHECK_EQ(r.rows.size(), (size_t)2);

  // CREATE INDEX, then a point query still returns the right row, and inserts
  // after index creation are maintained.
  db.execute("CREATE INDEX ix_id ON u(id);");
  db.execute("INSERT INTO u VALUES (3, 'Carol');");
  QueryResult q = db.execute("SELECT name FROM u WHERE id = 3;");
  CHECK(q.is_select);
  CHECK_EQ(q.rows.size(), (size_t)1);
  CHECK_EQ(q.rows[0][0].to_string(), std::string("Carol"));

  // ORDER BY returns rows in ascending key order.
  db.execute("CREATE TABLE n (v INT);");
  db.execute("INSERT INTO n VALUES (3);");
  db.execute("INSERT INTO n VALUES (1);");
  db.execute("INSERT INTO n VALUES (2);");
  QueryResult o = db.execute("SELECT v FROM n ORDER BY v;");
  CHECK_EQ(o.rows.size(), (size_t)3);
  CHECK_EQ(o.rows[0][0].as_int(), (int64_t)1);
  CHECK_EQ(o.rows[1][0].as_int(), (int64_t)2);
  CHECK_EQ(o.rows[2][0].as_int(), (int64_t)3);

  // End-to-end inner join through the full SQL path.
  db.execute("CREATE TABLE orders (uid INT, amt INT);");
  db.execute("INSERT INTO orders VALUES (1, 100);");
  db.execute("INSERT INTO orders VALUES (1, 200);");
  db.execute("INSERT INTO orders VALUES (3, 50);");
  QueryResult j = db.execute(
      "SELECT u.name, orders.amt FROM u INNER JOIN orders ON u.id = orders.uid "
      "ORDER BY orders.amt;");
  CHECK(j.is_select);
  CHECK_EQ(j.rows.size(), (size_t)3);          // (1,100),(1,200) for Alice; (3,50) for Carol
  CHECK_EQ(j.rows[0][1].as_int(), (int64_t)50);  // sorted by amt asc
  CHECK_EQ(j.rows[2][1].as_int(), (int64_t)200);
}

TEST_MAIN()
