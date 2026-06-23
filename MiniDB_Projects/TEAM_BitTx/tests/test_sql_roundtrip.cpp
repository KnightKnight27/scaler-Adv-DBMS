#include "catalog/catalog.h"
#include "common/types.h"
#include "execution/executor.h"
#include "parser/parser.h"
#include "parser/tokenizer.h"
#include "planner/planner.h"
#include "storage/disk_manager.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_rt.db");
  remove("/tmp/users.tbl");

  DiskManager dm("/tmp/minidb_rt.db");
  CatalogManager cat(&dm);
  vector<Column> cols = {Column("id", TypeId::INTEGER), Column("name", TypeId::VARCHAR)};
  assert(cat.CreateTable("users", Schema(cols)));

  TableHeap* t = cat.GetTable("users");
  (void)t;
  ExecutorContext ctx;
  (void)ctx;
  for (int i = 1; i <= 3; ++i) {
    string sql = "INSERT INTO users VALUES (" + to_string(i) + ", 'name" + to_string(i) + "');";
    Tokenizer tk(sql);
    Parser p(tk.TokenizeAll());
    auto stmt = p.ParseStatement();
    Planner planner(&cat);
    auto plan = planner.Plan(*stmt);
    assert(plan != nullptr);
    plan->Init();
    Tuple out;
    plan->Next(&out);
  }

  // SELECT all
  {
    string sql = "SELECT id FROM users;";
    Tokenizer tk(sql);
    Parser p(tk.TokenizeAll());
    auto stmt = p.ParseStatement();
    Planner planner(&cat);
    auto plan = planner.Plan(*stmt);
    plan->Init();
    Tuple out;
    int count = 0;
    while (plan->Next(&out))
      ++count;
    assert(count == 3);
  }

  remove("/tmp/minidb_rt.db");
  remove("/tmp/users.tbl");
  cout << "ALL SQL ROUNDTRIP TESTS PASSED" << endl;
  return 0;
}