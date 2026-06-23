#include <cassert>
#include <cstdio>
#include <iostream>
#include <memory>

#include "catalog/catalog.h"
#include "execution/executor.h"
#include "optimizer/optimizer.h"
#include "optimizer/stats.h"
#include "parser/parser.h"
#include "parser/tokenizer.h"
#include "planner/planner.h"
#include "storage/disk_manager.h"

using namespace minidb;
using namespace std;

static unique_ptr<AbstractExecutor> Run(CatalogManager* cat, const string& sql) {
  Tokenizer tk(sql);
  Parser p(tk.TokenizeAll());
  auto stmt = p.ParseStatement();
  Planner planner(cat);
  auto plan = planner.Plan(*stmt);
  auto stats = StatsCollector::Collect(cat);
  Optimizer opt(cat, stats);
  return opt.Optimize(move(plan));
}

static int32_t Drain(CatalogManager* cat, const string& sql) {
  auto exec = Run(cat, sql);
  exec->Init();
  int32_t total = 0;
  Tuple t;
  while (exec->Next(&t))
    total += t.GetValue(0).GetAsInteger();
  return total;
}

int main() {
  remove("/tmp/minidb_test_opt.db");
  DiskManager dm("/tmp/minidb_test_opt.db");
  CatalogManager cat(&dm);

  vector<Column> cols = {Column("v", TypeId::INTEGER)};
  assert(cat.CreateTable("t", Schema(cols)));
  TableHeap* t = cat.GetTable("t");
  for (int i = 1; i <= 100; ++i) {
    RecordId r;
    t->InsertTuple(Tuple({Value(i)}), &r);
  }

  // Aggregate: COUNT, SUM should be 100 and 5050 regardless of optimizer rewrites.
  assert(Drain(&cat, "SELECT v FROM t;") == 5050);
  // Limited scan.
  assert(Drain(&cat, "SELECT v FROM t ORDER BY v DESC LIMIT 5;") == 100 + 99 + 98 + 97 + 96);

  // Optimizer should successfully rewrite the plan even for join-free queries
  // without breaking results.
  auto stats = StatsCollector::Collect(&cat);
  Optimizer opt(&cat, stats);
  Tokenizer tk("SELECT v FROM t WHERE v > 50 ORDER BY v ASC;");
  Parser p(tk.TokenizeAll());
  auto stmt = p.ParseStatement();
  Planner planner(&cat);
  auto plan = planner.Plan(*stmt);
  assert(plan != nullptr);
  auto optimized = opt.Optimize(move(plan));
  assert(optimized != nullptr);
  optimized->Init();
  int32_t count = 0;
  Tuple tup;
  while (optimized->Next(&tup))
    ++count;
  assert(count == 50);

  cout << "test_optimizer passed\n";
  remove("/tmp/minidb_test_opt.db");
  return 0;
}