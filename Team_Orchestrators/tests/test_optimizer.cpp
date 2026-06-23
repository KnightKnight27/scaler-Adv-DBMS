#include "minidb/database.hpp"
#include "test_util.hpp"
#include <cstdio>
#include <string>
using namespace minidb;

static bool contains(const std::string& s, const std::string& sub) {
  return s.find(sub) != std::string::npos;
}

static void run_tests() {
  std::remove("t_opt.data");
  std::remove("t_opt.catalog");
  std::remove("t_opt.wal");
  Database db("t_opt");
  db.execute("CREATE TABLE big (id INT, v INT);");
  for (int i = 0; i < 200; ++i)
    db.execute("INSERT INTO big VALUES (" + std::to_string(i) + ", " +
               std::to_string(i % 5) + ");");
  db.execute("CREATE INDEX ix_id ON big(id);");
  db.execute("ANALYZE big;");

  // Selective equality on an indexed, high-distinct column -> IndexScan.
  QueryResult e1 = db.execute("EXPLAIN SELECT v FROM big WHERE id = 7;");
  CHECK(contains(e1.message, "IndexScan"));

  // Equality on an un-indexed column -> SeqScan.
  QueryResult e2 = db.execute("EXPLAIN SELECT id FROM big WHERE v = 2;");
  CHECK(contains(e2.message, "SeqScan"));

  // Join algorithm choice by cardinality.
  db.execute("CREATE TABLE a (x INT);");
  db.execute("CREATE TABLE b (y INT);");
  db.execute("INSERT INTO a VALUES (1);");
  db.execute("INSERT INTO b VALUES (1);");
  db.execute("ANALYZE a;");
  db.execute("ANALYZE b;");
  // Tiny inputs: nested-loop (|L|*|R|) is cheaper than hash (|L|+|R|).
  QueryResult e3 = db.execute("EXPLAIN SELECT a.x FROM a INNER JOIN b ON a.x = b.y;");
  CHECK(contains(e3.message, "NestedLoopJoin"));

  for (int i = 2; i < 60; ++i) {
    db.execute("INSERT INTO a VALUES (" + std::to_string(i) + ");");
    db.execute("INSERT INTO b VALUES (" + std::to_string(i) + ");");
  }
  db.execute("ANALYZE a;");
  db.execute("ANALYZE b;");
  // Larger inputs: hash join wins.
  QueryResult e4 = db.execute("EXPLAIN SELECT a.x FROM a INNER JOIN b ON a.x = b.y;");
  CHECK(contains(e4.message, "HashJoin"));
}

TEST_MAIN()
