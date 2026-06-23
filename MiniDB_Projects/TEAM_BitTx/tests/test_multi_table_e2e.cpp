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

static int32_t RunAgg(CatalogManager* cat, const string& sql) {
 Tokenizer tk(sql);
 Parser p(tk.TokenizeAll());
 auto stmt = p.ParseStatement();
 Planner planner(cat);
 auto plan = planner.Plan(*stmt);
 assert(plan != nullptr);
 plan->Init();
 int32_t total = 0;
 Tuple t;
 while (plan->Next(&t)) {
 total += t.GetValue(0).GetAsInteger();
 }
 return total;
}

static void Run(CatalogManager* cat, const string& sql) {
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
 remove("/tmp/minidb_test_mt.tbl");
 DiskManager dm("/tmp/minidb_test_mt.tbl");
 CatalogManager cat(&dm);

 // Two tables, joined later by aggregates across them.
 vector<Column> aCols = {Column("x", TypeId::INTEGER)};
 assert(cat.CreateTable("a", Schema(aCols)));
 vector<Column> bCols = {Column("y", TypeId::INTEGER)};
 assert(cat.CreateTable("b", Schema(bCols)));

 for (int i = 1; i <= 4; ++i) {
 Run(&cat, "INSERT INTO a VALUES (" + to_string(i) + ");");
 }
 for (int i = 1; i <= 3; ++i) {
 Run(&cat, "INSERT INTO b VALUES (" + to_string(i * 10) + ");");
 }

 // Aggregates on each table.
 assert(RunAgg(&cat, "SELECT x FROM a ORDER BY x ASC LIMIT 2;") == 1 + 2);
 assert(RunAgg(&cat, "SELECT y FROM b ORDER BY y DESC LIMIT 2;") == 30 + 20);

 // Cross-table GROUP BY on a single table.
 Run(&cat, "SELECT x FROM a GROUP BY x HAVING x > 2 ORDER BY x;");

 cout << "test_multi_table_e2e passed\n";
 remove("/tmp/minidb_test_mt.tbl");
 return 0;
}
