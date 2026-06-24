// Standalone verification driver for the SQL front-end:
//   lexer + parser (parser.h) and the cost-based optimizer (optimizer.h).
#include <cassert>
#include <iostream>
#include <string>

#include "execution.h"
#include "optimizer.h"
#include "parser.h"

using namespace minidb;

static int g_checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      std::cerr << "FAIL: " << #cond << " @ line " << __LINE__ << "\n"; \
      std::exit(1);                                                     \
    }                                                                   \
  } while (0)

static bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

static bool parseThrows(const std::string& sql) {
  try { Parser::parse(sql); } catch (const std::exception&) { return true; }
  return false;
}

static void testLexer() {
  auto toks = tokenize("SELECT name FROM users WHERE id >= 42");
  CHECK(toks.size() == 9);  // 8 tokens + End
  CHECK(toks[0].type == TokenType::Keyword && toks[0].text == "SELECT");
  CHECK(toks[1].type == TokenType::Ident && toks[1].text == "name");
  CHECK(toks[2].text == "FROM");
  CHECK(toks[6].type == TokenType::Op && toks[6].text == ">=");
  CHECK(toks[7].type == TokenType::IntLit && toks[7].int_val == 42);
  CHECK(toks.back().type == TokenType::End);

  // case-insensitive keywords, string + negative-int literals, qualified col
  auto t2 = tokenize("select u.id from t where name = 'a''b'");
  CHECK(t2[0].type == TokenType::Keyword);          // 'select' -> SELECT
  CHECK(t2[1].type == TokenType::Ident && t2[1].text == "u");
  CHECK(t2[2].type == TokenType::Dot);
  auto t3 = tokenize("VALUES (-7, 'x')");
  bool saw_neg = false;
  for (auto& t : t3) if (t.type == TokenType::IntLit && t.int_val == -7) saw_neg = true;
  CHECK(saw_neg);
  // escaped quote: 'a''b' -> a'b
  bool saw_quote = false;
  for (auto& t : t2) if (t.type == TokenType::StrLit && t.text == "a'b") saw_quote = true;
  CHECK(saw_quote);
  std::cout << "[ok] Lexer: keywords / ops / literals / qualified cols / quotes\n";
}

static void testParser() {
  // SELECT * FROM users
  {
    auto s = Parser::parse("SELECT * FROM users");
    CHECK(s.isSelect());
    const auto& sel = std::get<SelectStatement>(s.node);
    CHECK(sel.star && sel.from == "users" && !sel.join && !sel.where);
  }
  // projection + WHERE (single leaf)
  {
    auto s = Parser::parse("SELECT id, name FROM users WHERE id = 3");
    const auto& sel = std::get<SelectStatement>(s.node);
    CHECK(!sel.star && sel.columns.size() == 2);
    CHECK(sel.columns[1].column == "name");
    CHECK(sel.where && sel.where->kind == WhereExpr::Kind::Leaf);
    CHECK(sel.where->leaf.op == CompareOp::Eq && sel.where->leaf.val.i == 3);
  }
  // WHERE with AND/OR precedence: a = 1 AND b = 2 OR c = 3  parses as
  // (a=1 AND b=2) OR (c=3)  — AND binds tighter than OR.
  {
    auto s = Parser::parse("SELECT * FROM t WHERE a = 1 AND b = 2 OR c = 3");
    const auto& sel = std::get<SelectStatement>(s.node);
    CHECK(sel.where && sel.where->kind == WhereExpr::Kind::Or);
    CHECK(sel.where->left->kind == WhereExpr::Kind::And);
    CHECK(sel.where->left->left->leaf.col.column == "a");
    CHECK(sel.where->left->right->leaf.col.column == "b");
    CHECK(sel.where->right->kind == WhereExpr::Kind::Leaf);
    CHECK(sel.where->right->leaf.col.column == "c");
  }
  // Parentheses override precedence: a = 1 AND (b = 2 OR c = 3)
  {
    auto s = Parser::parse("SELECT * FROM t WHERE a = 1 AND (b = 2 OR c = 3)");
    const auto& sel = std::get<SelectStatement>(s.node);
    CHECK(sel.where->kind == WhereExpr::Kind::And);
    CHECK(sel.where->right->kind == WhereExpr::Kind::Or);
  }
  // JOIN
  {
    auto s = Parser::parse(
        "SELECT * FROM users JOIN orders ON users.id = orders.uid");
    const auto& sel = std::get<SelectStatement>(s.node);
    CHECK(sel.join.has_value());
    CHECK(sel.join->table == "orders");
    CHECK(sel.join->left.table == "users" && sel.join->left.column == "id");
    CHECK(sel.join->right.column == "uid");
  }
  // INSERT
  {
    auto s = Parser::parse("INSERT INTO users VALUES (7, 'grace')");
    CHECK(s.isInsert());
    const auto& ins = std::get<InsertStatement>(s.node);
    CHECK(ins.values.size() == 2);
    CHECK(ins.values[0].type == ValueType::Int && ins.values[0].i == 7);
    CHECK(ins.values[1].type == ValueType::Text && ins.values[1].s == "grace");
  }
  // DELETE + EXPLAIN
  {
    auto s = Parser::parse("EXPLAIN DELETE FROM users WHERE id = 9");
    CHECK(s.explain && s.isDelete());
    const auto& del = std::get<DeleteStatement>(s.node);
    CHECK(del.where && del.where->kind == WhereExpr::Kind::Leaf);
    CHECK(del.where->leaf.val.i == 9);
  }
  // error cases
  CHECK(parseThrows("SELECT FROM users"));        // empty select list
  CHECK(parseThrows("SELECT * users"));           // missing FROM
  CHECK(parseThrows("INSERT INTO t VALUES (1"));  // unclosed paren
  CHECK(parseThrows("UPDATE users SET x = 1"));   // unsupported stmt
  CHECK(parseThrows("SELECT * FROM users JOIN o ON a.x < b.y"));  // non-equi join
  CHECK(parseThrows("SELECT * FROM t WHERE (a = 1"));            // unbalanced paren
  CHECK(parseThrows("SELECT * FROM t WHERE a = 1 AND"));         // dangling AND
  std::cout << "[ok] Parser: select/insert/delete/join/explain + error paths\n";
}

static Catalog makeCatalog() {
  Catalog cat;
  Table* users = cat.createTable(
      "users", {{"id", ValueType::Int}, {"name", ValueType::Text}}, 0);
  Table* orders = cat.createTable(
      "orders", {{"uid", ValueType::Int}, {"item", ValueType::Text}}, 0);
  for (int i = 1; i <= 50; ++i)
    users->insert({Value::Int(i), Value::Text("user" + std::to_string(i))});
  orders->insert({Value::Int(2), Value::Text("book")});
  orders->insert({Value::Int(2), Value::Text("pen")});
  orders->insert({Value::Int(4), Value::Text("lamp")});
  return cat;
}

static void testOptimizer() {
  Catalog cat = makeCatalog();
  Optimizer opt(cat);
  ExecContext ctx;  // no lock manager: locking skipped (unit-test mode)

  // PK equality -> the planner must pick the index over a 50-row scan.
  {
    auto p = opt.optimize(Parser::parse("SELECT name FROM users WHERE id = 30"), ctx);
    CHECK(contains(p.explain, "IndexScan"));
    CHECK(contains(p.explain, "Projection"));
    auto rows = execute(*p.root);
    CHECK(rows.size() == 1 && rows[0][0].s == "user30");
  }

  // Non-PK predicate -> no usable index, fall back to TableScan + Filter.
  {
    auto p = opt.optimize(Parser::parse("SELECT * FROM users WHERE name = 'user7'"), ctx);
    CHECK(contains(p.explain, "TableScan"));
    CHECK(!contains(p.explain, "IndexScan"));
    auto rows = execute(*p.root);
    CHECK(rows.size() == 1 && rows[0][0].i == 7);
  }

  // PK range -> index range scan with the right bounds.
  {
    auto p = opt.optimize(Parser::parse("SELECT * FROM users WHERE id >= 48"), ctx);
    CHECK(contains(p.explain, "IndexScan"));
    auto rows = execute(*p.root);
    CHECK(rows.size() == 3);  // 48, 49, 50
    CHECK(rows.front()[0].i == 48 && rows.back()[0].i == 50);
  }

  // PK '>' needs a residual Filter on the index bound (excludes the key).
  {
    auto p = opt.optimize(Parser::parse("SELECT * FROM users WHERE id > 48"), ctx);
    CHECK(contains(p.explain, "IndexScan"));
    CHECK(contains(p.explain, "Filter"));
    auto rows = execute(*p.root);
    CHECK(rows.size() == 2);  // 49, 50 (not 48)
  }

  // Full scan, no predicate.
  {
    auto p = opt.optimize(Parser::parse("SELECT * FROM users"), ctx);
    CHECK(contains(p.explain, "TableScan"));
    CHECK(execute(*p.root).size() == 50);
  }

  // AND over a PK range: index narrows on one conjunct, Filter re-checks both.
  {
    auto p = opt.optimize(
        Parser::parse("SELECT * FROM users WHERE id >= 10 AND id <= 12"), ctx);
    CHECK(contains(p.explain, "IndexScan"));
    CHECK(contains(p.explain, "Filter["));   // residual re-check of the AND
    auto rows = execute(*p.root);
    CHECK(rows.size() == 3);  // 10, 11, 12
  }

  // OR cannot use the index (other branch could have any key) -> scan + filter.
  {
    auto p = opt.optimize(
        Parser::parse("SELECT * FROM users WHERE id = 5 OR id = 9"), ctx);
    CHECK(contains(p.explain, "TableScan"));
    CHECK(!contains(p.explain, "IndexScan"));
    auto rows = execute(*p.root);
    CHECK(rows.size() == 2);  // 5 and 9
  }

  // Mixed AND/OR across PK and non-PK columns.
  {
    auto p = opt.optimize(
        Parser::parse("SELECT * FROM users WHERE id < 3 OR name = 'user40'"), ctx);
    auto rows = execute(*p.root);
    CHECK(rows.size() == 3);  // id 1, id 2, user40
  }

  // JOIN: orders(3 rows) must drive the loop over users(50 rows).
  {
    auto p = opt.optimize(
        Parser::parse("SELECT * FROM users JOIN orders ON users.id = orders.uid"), ctx);
    CHECK(contains(p.explain, "NestedLoopJoin"));
    CHECK(contains(p.explain, "outer=orders"));      // reordered to smaller side
    CHECK(contains(p.explain, "reordered"));
    auto rows = execute(*p.root);
    CHECK(rows.size() == 3);          // uid 2 x2, uid 4 x1
    for (auto& r : rows) CHECK(r.size() == 4);
  }

  // JOIN + single-table WHERE: the predicate is pushed below the join and uses
  // the users PK index; result is enroll rows for that user.
  {
    auto p = opt.optimize(
        Parser::parse("SELECT * FROM users JOIN orders ON users.id = orders.uid "
                      "WHERE users.id = 2"), ctx);
    CHECK(contains(p.explain, "NestedLoopJoin"));
    CHECK(contains(p.explain, "IndexScan(users)"));  // WHERE pushed to users
    auto rows = execute(*p.root);
    CHECK(rows.size() == 2);  // user2 ⨝ {book, pen}
  }

  // JOIN + cross-table WHERE: applied as a Filter over the join output.
  {
    auto p = opt.optimize(
        Parser::parse("SELECT * FROM users JOIN orders ON users.id = orders.uid "
                      "WHERE users.id >= 4 AND orders.item = 'lamp'"), ctx);
    CHECK(contains(p.explain, "Filter["));  // post-join filter spanning both
    auto rows = execute(*p.root);
    CHECK(rows.size() == 1);  // user4 ⨝ lamp
  }

  std::cout << "[ok] Optimizer: index-vs-scan / AND-OR / join order / pushdown\n";
}

static void testDml() {
  Catalog cat = makeCatalog();
  Optimizer opt(cat);
  ExecContext ctx;
  Table* users = cat.getTable("users");

  // INSERT through the planner.
  {
    auto p = opt.optimize(Parser::parse("INSERT INTO users VALUES (51, 'newbie')"), ctx);
    CHECK(p.is_dml);
    execute(*p.root);
    auto* ins = dynamic_cast<Insert*>(p.root.get());
    CHECK(ins && ins->inserted() == 1);
    CHECK(users->index().search(51).has_value());
  }

  // DELETE WHERE pk = k uses the index path, then tombstones one row.
  {
    auto p = opt.optimize(Parser::parse("DELETE FROM users WHERE id = 51"), ctx);
    CHECK(p.is_dml && contains(p.explain, "IndexScan"));
    execute(*p.root);
    auto* del = dynamic_cast<Delete*>(p.root.get());
    CHECK(del && del->deleted() == 1);
    CHECK(!users->index().search(51).has_value());
  }

  // Semantic errors surface from the planner.
  bool threw = false;
  try { opt.optimize(Parser::parse("SELECT * FROM nope"), ctx); }
  catch (const std::exception&) { threw = true; }
  CHECK(threw);

  threw = false;
  try { opt.optimize(Parser::parse("INSERT INTO users VALUES (1)"), ctx); }  // arity
  catch (const std::exception&) { threw = true; }
  CHECK(threw);

  std::cout << "[ok] DML: insert/delete planning + semantic checks\n";
}

int main() {
  testLexer();
  testParser();
  testOptimizer();
  testDml();
  std::cout << "\nAll Track 4 (parser + optimizer) checks passed (" << g_checks
            << " assertions).\n";
  return 0;
}
