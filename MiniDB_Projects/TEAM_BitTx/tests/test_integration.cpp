#include "catalog/catalog.h"
#include "common/types.h"
#include "executor/evaluator.h"
#include "parser/parser.h"
#include "parser/tokenizer.h"
#include "planner/planner.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  // 1. Build schema
  vector<Column> cols = {Column("id", TypeId::INTEGER), Column("name", TypeId::VARCHAR)};
  Schema schema(cols);
  assert(schema.GetColumnIndex("id") == 0);
  assert(schema.GetColumnIndex("name") == 1);

  // 2. Eval column ref against row
  vector<Value> row = {Value(42), Value("alice")};
  {
    auto e = Evaluator::Eval; // (avoid unused)
    (void)e;
  }
  // Build ColumnRef manually
  ColumnRef cr;
  cr.columnName = "id";
  int64_t v = Evaluator::Eval(cr, row, &schema).GetAsInteger();
  assert(v == 42);

  // 3. Parse + plan a real SQL
  string sql = "SELECT id FROM users WHERE id = 1;";
  Tokenizer tk(sql);
  Parser p(tk.TokenizeAll());
  auto stmt = p.ParseStatement();
  assert(stmt->GetType() == StmtType::SELECT);
  auto sel = static_cast<SelectStmt*>(stmt.get());
  assert(sel->fromTable == "users");

  cout << "ALL INTEGRATION TESTS PASSED" << endl;
  return 0;
}