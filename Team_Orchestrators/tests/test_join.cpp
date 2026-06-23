#include "minidb/catalog.hpp"
#include "minidb/exec/operators.hpp"
#include "minidb/sql/parser.hpp"
#include "minidb/storage/heap_engine.hpp"
#include "test_util.hpp"
#include <cstdio>
#include <memory>
using namespace minidb;

static void test_grammar() {
  Parser p("SELECT a.x, b.y FROM a INNER JOIN b ON a.x = b.y;");
  Statement s = p.parse();
  CHECK(s.kind == StatementKind::Select);
  CHECK(s.select.join.present);
  CHECK_EQ(s.select.table, std::string("a"));
  CHECK_EQ(s.select.join.table, std::string("b"));
  CHECK_EQ(s.select.join.left_col, std::string("a.x"));
  CHECK_EQ(s.select.join.right_col, std::string("b.y"));
  CHECK_EQ(s.select.columns.size(), (size_t)2);
  CHECK_EQ(s.select.columns[0], std::string("a.x"));
  CHECK_EQ(s.select.columns[1], std::string("b.y"));

  Parser p2("SELECT x FROM t WHERE x = 1;");
  Statement s2 = p2.parse();
  CHECK(!s2.select.join.present);
}

static void test_operators() {
  std::remove("t_join.data");
  std::remove("t_join.catalog");
  Catalog cat;
  Schema users_schema({{"id", TypeId::Int, false}, {"name", TypeId::Varchar, false}});
  Schema orders_schema({{"uid", TypeId::Int, false}, {"amt", TypeId::Int, false}});
  TableId users = cat.create_table("users", users_schema);
  TableId orders = cat.create_table("orders", orders_schema);
  HeapEngine eng(cat, "t_join.data");
  eng.insert(users, Tuple{Value((int64_t)1), Value(std::string("Alice"))});
  eng.insert(users, Tuple{Value((int64_t)2), Value(std::string("Bob"))});
  eng.insert(orders, Tuple{Value((int64_t)1), Value((int64_t)100)});
  eng.insert(orders, Tuple{Value((int64_t)1), Value((int64_t)200)});
  eng.insert(orders, Tuple{Value((int64_t)2), Value((int64_t)50)});
  eng.insert(orders, Tuple{Value((int64_t)9), Value((int64_t)999)});  // no match

  // Joined schema: users.id, users.name, orders.uid, orders.amt.
  Schema joined({{"users.id", TypeId::Int, false},
                 {"users.name", TypeId::Varchar, false},
                 {"orders.uid", TypeId::Int, false},
                 {"orders.amt", TypeId::Int, false}});

  // Join on users.id (idx 0) == orders.uid (idx 0); 3 matching pairs expected.
  auto run = [&](bool hash) -> size_t {
    auto l = std::unique_ptr<Operator>(new SeqScanOp(eng, users, users_schema));
    auto r = std::unique_ptr<Operator>(new SeqScanOp(eng, orders, orders_schema));
    std::unique_ptr<Operator> op;
    if (hash)
      op.reset(new HashJoinOp(std::move(l), std::move(r), 0, 0, joined));
    else
      op.reset(new NestedLoopJoinOp(std::move(l), std::move(r), 0, 0, joined));
    op->open();
    size_t n = 0;
    while (Optional<Tuple> t = op->next()) {
      CHECK_EQ(t->size(), (size_t)4);  // concatenated width
      ++n;
    }
    op->close();
    return n;
  };
  CHECK_EQ(run(true), (size_t)3);
  CHECK_EQ(run(false), (size_t)3);
}

static void run_tests() {
  test_grammar();
  test_operators();
}

TEST_MAIN()
