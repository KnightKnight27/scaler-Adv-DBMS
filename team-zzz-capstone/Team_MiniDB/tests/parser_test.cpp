#include <cassert>
#include <iostream>

#include "sql/lexer.h"
#include "sql/parser.h"

using namespace minidb;

Statement Parse(const std::string &sql) {
  Lexer lex(sql);
  Parser parser(lex.Tokenize());
  return parser.ParseStatement();
}

int main() {
  {
    auto s = Parse("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR)");
    assert(s.kind == StmtKind::CREATE_TABLE);
    assert(s.create_table.table_name == "users");
    assert(s.create_table.columns.size() == 2);
    assert(s.create_table.columns[0].is_primary_key);
    assert(!s.create_table.columns[1].is_primary_key);
  }
  {
    auto s = Parse("INSERT INTO users VALUES (1, 'alice')");
    assert(s.kind == StmtKind::INSERT);
    assert(s.insert.table_name == "users");
    assert(s.insert.values.size() == 2);
    assert(s.insert.values[0].AsInt() == 1);
    assert(s.insert.values[1].AsStr() == "alice");
  }
  {
    auto s = Parse("SELECT id, name FROM users WHERE id = 1");
    assert(s.kind == StmtKind::SELECT);
    assert(s.select.select_cols.size() == 2);
    assert(s.select.where.size() == 1);
    assert(s.select.where[0].left.column.column == "id");
    assert(s.select.where[0].right.literal.AsInt() == 1);
  }
  {
    auto s = Parse("SELECT * FROM orders o JOIN users u ON o.user_id = u.id WHERE u.id > 5 AND o.amount < 100");
    assert(s.kind == StmtKind::SELECT);
    assert(s.select.select_cols.empty());
    assert(s.select.has_join);
    assert(s.select.join_table == "users");
    assert(s.select.join_alias == "u");
    assert(s.select.join_cond.left.column.table == "o");
    assert(s.select.where.size() == 2);
  }
  {
    auto s = Parse("DELETE FROM users WHERE id = 1");
    assert(s.kind == StmtKind::DELETE);
    assert(s.del.where.size() == 1);
  }
  std::cout << "parser_test PASSED\n";
  return 0;
}
