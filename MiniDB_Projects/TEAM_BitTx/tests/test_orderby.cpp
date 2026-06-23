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

int main() {
  remove("/tmp/minidb_test_ob.tbl");
  DiskManager dm("/tmp/minidb_test_ob.tbl");
  CatalogManager cat(&dm);
  vector<Column> cols = {Column("v", TypeId::INTEGER)};
  assert(cat.CreateTable("t", Schema(cols)));
  TableHeap* t = cat.GetTable("t");
  RecordId r;
  t->InsertTuple(Tuple({Value(3)}), &r);
  t->InsertTuple(Tuple({Value(1)}), &r);
  t->InsertTuple(Tuple({Value(2)}), &r);

  string sql = "SELECT * FROM t ORDER BY v ASC;";
  Tokenizer tk(sql);
  Parser p(tk.TokenizeAll());
  auto stmt = p.ParseStatement();
  Planner planner(&cat);
  auto plan = planner.Plan(*stmt);
  assert(plan != nullptr);
  plan->Init();
  vector<int32_t> out;
  Tuple tup;
  while (plan->Next(&tup)) {
    out.push_back(tup.GetValue(0).GetAsInteger());
  }
  assert(out.size() == 3);
  assert(out[0] == 1);
  assert(out[1] == 2);
  assert(out[2] == 3);
  cout << "test_orderby passed\n";
  remove("/tmp/minidb_test_ob.tbl");
  return 0;
}
