#include "minidb/sql/parser.hpp"

#include <stdexcept>

namespace minidb {

Parser::Parser(std::string sql) {
  Lexer lex(std::move(sql));
  toks_ = lex.tokenize();
}

bool Parser::at_keyword(const char* kw) const {
  return peek().kind == TokKind::Keyword && peek().text == kw;
}

const Token& Parser::expect(TokKind k, const char* what) {
  if (peek().kind != k)
    throw std::runtime_error(std::string("parse error: expected ") + what +
                             " but got '" + peek().text + "'");
  return advance();
}

void Parser::expect_keyword(const char* kw) {
  if (!at_keyword(kw))
    throw std::runtime_error(std::string("parse error: expected keyword '") + kw +
                             "' but got '" + peek().text + "'");
  advance();
}

Statement Parser::parse() {
  if (!check(TokKind::Keyword))
    throw std::runtime_error("parse error: expected a statement keyword");
  const std::string& kw = peek().text;
  Statement stmt;
  if (kw == "create") {
    // CREATE TABLE ... vs CREATE INDEX ...
    if (pos_ + 1 < toks_.size() && toks_[pos_ + 1].kind == TokKind::Keyword &&
        toks_[pos_ + 1].text == "index") {
      stmt.kind = StatementKind::CreateIndex;
      stmt.create_index = parse_create_index();
    } else {
      stmt.kind = StatementKind::CreateTable;
      stmt.create = parse_create();
    }
  } else if (kw == "insert") {
    stmt.kind = StatementKind::Insert;
    stmt.insert = parse_insert();
  } else if (kw == "select") {
    stmt.kind = StatementKind::Select;
    stmt.select = parse_select();
  } else if (kw == "delete") {
    stmt.kind = StatementKind::Delete;
    stmt.remove = parse_delete();
  } else if (kw == "begin") {
    stmt.kind = StatementKind::Begin;
    advance();
  } else if (kw == "commit") {
    stmt.kind = StatementKind::Commit;
    advance();
  } else if (kw == "rollback") {
    stmt.kind = StatementKind::Rollback;
    advance();
  } else if (kw == "analyze") {
    stmt.kind = StatementKind::Analyze;
    advance();
    stmt.analyze.table = expect(TokKind::Ident, "table name").text;
  } else if (kw == "explain") {
    stmt.kind = StatementKind::Explain;
    advance();
    stmt.explain.select = parse_select();
  } else {
    throw std::runtime_error("parse error: unsupported statement '" + kw + "'");
  }

  if (check(TokKind::Semicolon)) advance();
  if (!check(TokKind::End))
    throw std::runtime_error("parse error: trailing tokens after statement");
  return stmt;
}

CreateTableStmt Parser::parse_create() {
  expect_keyword("create");
  expect_keyword("table");
  CreateTableStmt s;
  s.table = expect(TokKind::Ident, "table name").text;
  expect(TokKind::LParen, "'('");
  while (true) {
    Column col;
    col.name = expect(TokKind::Ident, "column name").text;
    if (!check(TokKind::Keyword))
      throw std::runtime_error("parse error: expected a column type");
    const std::string& ty = advance().text;
    if (ty == "int" || ty == "integer") col.type = TypeId::Int;
    else if (ty == "double" || ty == "float") col.type = TypeId::Double;
    else if (ty == "varchar" || ty == "text") {
      col.type = TypeId::Varchar;
      if (check(TokKind::LParen)) {  // VARCHAR(n) — n is advisory in v1
        advance();
        expect(TokKind::IntLit, "length");
        expect(TokKind::RParen, "')'");
      }
    } else {
      throw std::runtime_error("parse error: unknown type '" + ty + "'");
    }
    if (at_keyword("primary")) { advance(); expect_keyword("key"); col.primary_key = true; }
    s.columns.push_back(col);
    if (check(TokKind::Comma)) { advance(); continue; }
    break;
  }
  expect(TokKind::RParen, "')'");
  return s;
}

CreateIndexStmt Parser::parse_create_index() {
  expect_keyword("create");
  expect_keyword("index");
  CreateIndexStmt s;
  s.name = expect(TokKind::Ident, "index name").text;
  expect_keyword("on");
  s.table = expect(TokKind::Ident, "table name").text;
  expect(TokKind::LParen, "'('");
  s.column = expect(TokKind::Ident, "column name").text;
  expect(TokKind::RParen, "')'");
  return s;
}

InsertStmt Parser::parse_insert() {
  expect_keyword("insert");
  expect_keyword("into");
  InsertStmt s;
  s.table = expect(TokKind::Ident, "table name").text;
  expect_keyword("values");
  expect(TokKind::LParen, "'('");
  while (true) {
    s.values.push_back(parse_literal());
    if (check(TokKind::Comma)) { advance(); continue; }
    break;
  }
  expect(TokKind::RParen, "')'");
  return s;
}

SelectStmt Parser::parse_select() {
  expect_keyword("select");
  SelectStmt s;
  if (check(TokKind::Star)) {
    advance();  // SELECT * => columns empty
  } else {
    while (true) {
      s.columns.push_back(parse_column_ref());
      if (check(TokKind::Comma)) { advance(); continue; }
      break;
    }
  }
  expect_keyword("from");
  s.table = expect(TokKind::Ident, "table name").text;
  if (at_keyword("inner")) {
    advance();
    expect_keyword("join");
    s.join.present = true;
    s.join.table = expect(TokKind::Ident, "table name").text;
    expect_keyword("on");
    s.join.left_col = parse_column_ref();
    expect(TokKind::Eq, "'=' (equi-join only)");
    s.join.right_col = parse_column_ref();
  }
  if (at_keyword("where")) s.where = parse_where();
  if (at_keyword("order")) s.order_by = parse_order_by();
  return s;
}

std::string Parser::parse_column_ref() {
  std::string name = expect(TokKind::Ident, "column name").text;
  if (check(TokKind::Dot)) {
    advance();
    name += ".";
    name += expect(TokKind::Ident, "column name").text;
  }
  return name;
}

DeleteStmt Parser::parse_delete() {
  expect_keyword("delete");
  expect_keyword("from");
  DeleteStmt s;
  s.table = expect(TokKind::Ident, "table name").text;
  if (at_keyword("where")) s.where = parse_where();
  return s;
}

WhereClause Parser::parse_where() {
  expect_keyword("where");
  WhereClause where;
  while (true) {
    Predicate p;
    p.column = parse_column_ref();
    p.op = parse_cmp();
    p.value = parse_literal();
    where.push_back(std::move(p));
    if (at_keyword("and")) { advance(); continue; }
    break;
  }
  return where;
}

CmpOp Parser::parse_cmp() {
  switch (peek().kind) {
    case TokKind::Eq: advance(); return CmpOp::Eq;
    case TokKind::Ne: advance(); return CmpOp::Ne;
    case TokKind::Lt: advance(); return CmpOp::Lt;
    case TokKind::Le: advance(); return CmpOp::Le;
    case TokKind::Gt: advance(); return CmpOp::Gt;
    case TokKind::Ge: advance(); return CmpOp::Ge;
    default:
      throw std::runtime_error("parse error: expected a comparison operator");
  }
}

std::vector<std::string> Parser::parse_order_by() {
  expect_keyword("order");
  expect_keyword("by");
  std::vector<std::string> columns;
  while (true) {
    columns.push_back(parse_column_ref());
    if (check(TokKind::Comma)) { advance(); continue; }
    break;
  }
  return columns;
}

Value Parser::parse_literal() {
  const Token& t = peek();
  switch (t.kind) {
    case TokKind::IntLit: advance(); return Value(static_cast<int64_t>(t.int_val));
    case TokKind::DoubleLit: advance(); return Value(t.dbl_val);
    case TokKind::StringLit: advance(); return Value(t.text);
    default:
      throw std::runtime_error("parse error: expected a literal value");
  }
}

}  // namespace minidb
