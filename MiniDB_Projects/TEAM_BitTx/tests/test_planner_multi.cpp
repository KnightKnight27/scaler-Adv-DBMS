// Multi-statement planner test for MiniDB.
// Plans CREATE, INSERT, SELECT through the same Planner in one process.
#include <cassert>
#include <cstdio>
#include <iostream>

#include "catalog/catalog.h"
#include "common/types.h"
#include "execution/executor.h"
#include "parser/parser.h"
#include "parser/tokenizer.h"
#include "planner/planner.h"
#include "storage/disk_manager.h"

using namespace minidb;
using namespace std;

static void Run(CatalogManager* cat, const string& sql) {
  Tokenizer tk(sql);
  Parser p(tk.TokenizeAll());
  auto stmt = p.ParseStatement();
  Planner planner(cat);
  auto plan = planner.Plan(*stmt);
  if (plan) {
    plan->Init();
    Tuple t;
    while (plan->Next(&t)) {
    }
  }
}

int main() {
  remove("/tmp/minidb_plan_multi.db");
  remove("/tmp/items.tbl");

  DiskManager dm("/tmp/minidb_plan_multi.db");
  CatalogManager cat(&dm);

  Run(&cat, "CREATE TABLE items (id INT, name VARCHAR);");
  Run(&cat, "INSERT INTO items VALUES (1, 'apple');");
  Run(&cat, "INSERT INTO items VALUES (2, 'banana');");
  Run(&cat, "INSERT INTO items VALUES (3, 'cherry');");

  int32_t count = 0;
  {
    Tokenizer tk("SELECT id FROM items;");
    Parser p(tk.TokenizeAll());
    auto stmt = p.ParseStatement();
    Planner planner(&cat);
    auto plan = planner.Plan(*stmt);
    plan->Init();
    Tuple t;
    while (plan->Next(&t))
      ++count;
  }
  assert(count == 3);

  remove("/tmp/minidb_plan_multi.db");
  remove("/tmp/items.tbl");
  cout << "ALL PLANNER MULTI TESTS PASSED" << endl;
  return 0;
}
