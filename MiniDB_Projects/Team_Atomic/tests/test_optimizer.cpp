// Verifies the cost-based optimizer: index-scan vs table-scan selection and
// join-order (outer/inner) selection. Checks both the chosen plan string and
// that results stay correct regardless of plan.
#include <cassert>
#include <cstdio>
#include <iostream>
#include "engine/database.h"

using namespace minidb;

int main() {
  std::remove("t_opt.db");
  std::remove("t_opt.catalog");
  Database db("t_opt");

  db.Execute("CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER)");
  for (int i = 1; i <= 1000; i++)
    db.Execute("INSERT INTO t VALUES (" + std::to_string(i) + ", " +
               std::to_string(i % 10) + ")");

  // Selective equality on PK -> index scan.
  auto eq = db.Execute("SELECT id FROM t WHERE id = 777");
  std::cout << "  plan: " << eq.plan << "\n";
  assert(eq.rows.size() == 1 && eq.rows[0][0].i == 777);
  assert(eq.plan.find("IndexScan") != std::string::npos);

  // Small range on PK -> index scan.
  auto rng = db.Execute("SELECT id FROM t WHERE id >= 10 AND id <= 19");
  std::cout << "  plan: " << rng.plan << "\n";
  assert(rng.rows.size() == 10);
  assert(rng.plan.find("IndexScan") != std::string::npos);

  // Predicate on a non-PK column -> table scan (no usable index).
  auto ns = db.Execute("SELECT id FROM t WHERE v = 3");
  std::cout << "  plan: " << ns.plan << "\n";
  assert(ns.rows.size() == 100);
  assert(ns.plan.find("SeqScan") != std::string::npos);

  // Wide PK range covering most rows -> table scan is cheaper.
  auto wide = db.Execute("SELECT id FROM t WHERE id >= 1");
  std::cout << "  plan: " << wide.plan << "\n";
  assert(wide.rows.size() == 1000);
  assert(wide.plan.find("SeqScan") != std::string::npos);

  // Join-order: build a tiny table and a big table; the optimizer should make
  // the smaller one the outer relation.
  db.Execute("CREATE TABLE small (sid INTEGER PRIMARY KEY, ref INTEGER)");
  db.Execute("INSERT INTO small VALUES (1, 5)");
  db.Execute("INSERT INTO small VALUES (2, 50)");
  auto jn = db.Execute(
      "SELECT small.sid, t.v FROM t JOIN small ON t.id = small.ref");
  std::cout << "  plan: " << jn.plan << "\n";
  assert(jn.rows.size() == 2);
  assert(jn.plan.find("outer=small") != std::string::npos);

  std::remove("t_opt.db");
  std::remove("t_opt.catalog");
  std::cout << "[optimizer] index/seq selection + join ordering OK\n";
  return 0;
}
