#include "parser.h"

#include <stdexcept>

namespace minidb {

const Token &Parser::Expect(TokenType t, const char *what) {
  if (Peek().type != t) throw std::runtime_error(std::string("parse error: expected ") + what);
  return Advance();
}

Statement Parser::ParseStatement() {
  Statement stmt;
  if (Check(TokenType::KW_CREATE)) {
    stmt.kind = StmtKind::CREATE_TABLE;
    stmt.create_table = ParseCreateTable();
  } else if (Check(TokenType::KW_INSERT)) {
    stmt.kind = StmtKind::INSERT;
    stmt.insert = ParseInsert();
  } else if (Check(TokenType::KW_SELECT)) {
    stmt.kind = StmtKind::SELECT;
    stmt.select = ParseSelect();
  } else if (Check(TokenType::KW_DELETE)) {
    stmt.kind = StmtKind::DELETE;
    stmt.del = ParseDelete();
  } else {
    throw std::runtime_error("parse error: unrecognized statement");
  }
  if (Check(TokenType::SEMI)) Advance();
  return stmt;
}

CreateTableStmt Parser::ParseCreateTable() {
  Expect(TokenType::KW_CREATE, "CREATE");
  Expect(TokenType::KW_TABLE, "TABLE");
  CreateTableStmt s;
  s.table_name = Expect(TokenType::IDENT, "table name").text;
  Expect(TokenType::LPAREN, "(");
  while (true) {
    Column col;
    col.name = Expect(TokenType::IDENT, "column name").text;
    if (Check(TokenType::KW_INT)) { col.type = TypeId::INTEGER; Advance(); }
    else if (Check(TokenType::KW_VARCHAR)) { col.type = TypeId::VARCHAR; Advance(); }
    else throw std::runtime_error("parse error: expected column type");
    if (Check(TokenType::KW_PRIMARY)) {
      Advance();
      Expect(TokenType::KW_KEY, "KEY");
      col.is_primary_key = true;
    }
    s.columns.push_back(col);
    if (Check(TokenType::COMMA)) { Advance(); continue; }
    break;
  }
  Expect(TokenType::RPAREN, ")");
  return s;
}

InsertStmt Parser::ParseInsert() {
  Expect(TokenType::KW_INSERT, "INSERT");
  Expect(TokenType::KW_INTO, "INTO");
  InsertStmt s;
  s.table_name = Expect(TokenType::IDENT, "table name").text;
  Expect(TokenType::KW_VALUES, "VALUES");
  Expect(TokenType::LPAREN, "(");
  while (true) {
    s.values.push_back(ParseLiteral());
    if (Check(TokenType::COMMA)) { Advance(); continue; }
    break;
  }
  Expect(TokenType::RPAREN, ")");
  return s;
}

DeleteStmt Parser::ParseDelete() {
  Expect(TokenType::KW_DELETE, "DELETE");
  Expect(TokenType::KW_FROM, "FROM");
  DeleteStmt s;
  s.table_name = Expect(TokenType::IDENT, "table name").text;
  if (Check(TokenType::KW_WHERE)) {
    Advance();
    while (true) {
      s.where.push_back(ParseComparison());
      if (Check(TokenType::KW_AND)) { Advance(); continue; }
      break;
    }
  }
  return s;
}

SelectStmt Parser::ParseSelect() {
  Expect(TokenType::KW_SELECT, "SELECT");
  SelectStmt s;
  if (Check(TokenType::STAR)) {
    Advance();
  } else {
    while (true) {
      s.select_cols.push_back(ParseColumnRef());
      if (Check(TokenType::COMMA)) { Advance(); continue; }
      break;
    }
  }
  Expect(TokenType::KW_FROM, "FROM");
  s.from_table = Expect(TokenType::IDENT, "table name").text;
  s.from_alias = s.from_table;
  if (Check(TokenType::KW_AS)) { Advance(); s.from_alias = Expect(TokenType::IDENT, "alias").text; }
  else if (Check(TokenType::IDENT)) { s.from_alias = Advance().text; }

  if (Check(TokenType::KW_JOIN)) {
    Advance();
    s.has_join = true;
    s.join_table = Expect(TokenType::IDENT, "join table name").text;
    s.join_alias = s.join_table;
    if (Check(TokenType::KW_AS)) { Advance(); s.join_alias = Expect(TokenType::IDENT, "alias").text; }
    else if (Check(TokenType::IDENT)) { s.join_alias = Advance().text; }
    Expect(TokenType::KW_ON, "ON");
    s.join_cond = ParseComparison();
  }

  if (Check(TokenType::KW_WHERE)) {
    Advance();
    while (true) {
      s.where.push_back(ParseComparison());
      if (Check(TokenType::KW_AND)) { Advance(); continue; }
      break;
    }
  }
  return s;
}

Comparison Parser::ParseComparison() {
  Comparison c;
  c.left = ParseOperand();
  CompareOp op;
  switch (Advance().type) {
    case TokenType::EQ: op = CompareOp::EQ; break;
    case TokenType::NEQ: op = CompareOp::NEQ; break;
    case TokenType::LT: op = CompareOp::LT; break;
    case TokenType::LTE: op = CompareOp::LTE; break;
    case TokenType::GT: op = CompareOp::GT; break;
    case TokenType::GTE: op = CompareOp::GTE; break;
    default: throw std::runtime_error("parse error: expected comparison operator");
  }
  c.op = op;
  c.right = ParseOperand();
  return c;
}

Operand Parser::ParseOperand() {
  Operand o;
  if (Check(TokenType::NUMBER) || Check(TokenType::STRING)) {
    o.is_column = false;
    o.literal = ParseLiteral();
  } else {
    o.is_column = true;
    o.column = ParseColumnRef();
  }
  return o;
}

ColumnRef Parser::ParseColumnRef() {
  ColumnRef ref;
  std::string first = Expect(TokenType::IDENT, "identifier").text;
  if (Check(TokenType::DOT)) {
    Advance();
    ref.table = first;
    ref.column = Expect(TokenType::IDENT, "column name").text;
  } else {
    ref.column = first;
  }
  return ref;
}

Value Parser::ParseLiteral() {
  if (Check(TokenType::NUMBER)) return Value::Int(std::stoll(Advance().text));
  if (Check(TokenType::STRING)) return Value::Str(Advance().text);
  throw std::runtime_error("parse error: expected literal value");
}

}  // namespace minidb
