#include <cassert>
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

#include "catalog/catalog.h"
#include "common/types.h"
#include "execution/executor.h"
#include "parser/parser.h"
#include "parser/tokenizer.h"
#include "planner/planner.h"
#include "storage/disk_manager.h"

using namespace minidb;
using namespace std;

static void Plan(CatalogManager* cat, const string& sql) {
  Tokenizer tk(sql);
  Parser p(tk.TokenizeAll());
  auto stmt = p.ParseStatement();
  Planner planner(cat);
  auto plan = planner.Plan(*stmt);
  assert(plan != nullptr);
  plan->Init();
  Tuple t;
  while (plan->Next(&t)) {
    // drain
  }
}

int main() {
  remove("/tmp/minidb_test_hl.tbl");
  DiskManager dm("/tmp/minidb_test_hl.tbl");
  CatalogManager cat(&dm);
  vector<Column> cols = {Column("g", TypeId::INTEGER), Column("v", TypeId::INTEGER)};
  assert(cat.CreateTable("t", Schema(cols)));
  TableHeap* t = cat.GetTable("t");
  RecordId r;
  for (int g = 0; g < 3; ++g) {
    for (int v = 0; v < 3; ++v) {
      t->InsertTuple(Tuple({Value(g), Value(v * 10)}), &r);
    }
  }

  Plan(&cat, "SELECT g, v FROM t ORDER BY v DESC LIMIT 2;");
  Plan(&cat, "SELECT g, v FROM t GROUP BY g, v HAVING v > 0;");
  Plan(&cat, "SELECT g FROM t GROUP BY g HAVING g > 0 ORDER BY g ASC LIMIT 1;");

  cout << "test_having_limit passed\n";
  remove("/tmp/minidb_test_hl.tbl");
  return 0;
}
