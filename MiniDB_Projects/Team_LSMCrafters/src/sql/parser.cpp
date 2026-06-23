#include "sql/parser.h"
#include <stdexcept>

namespace minidb {

bool Parser::match(TokenType type) {
  if (peek().type == type) { ++pos_; return true; }
  return false;
}

const Token& Parser::expect(TokenType type, const char* what) {
  if (peek().type != type) throw std::runtime_error(std::string("parse error: expected ") + what);
  return advance();
}

Statement Parser::parse() {
  if (match(TokenType::Explain)) explain_ = true;
  switch (peek().type) {
    case TokenType::Select: return parse_select();
    case TokenType::Insert: return parse_insert();
    case TokenType::Delete: return parse_delete();
    default: throw std::runtime_error("parse error: expected SELECT, INSERT, or DELETE");
  }
}

ColumnRef Parser::parse_column_ref() {
  const std::string& text = expect(TokenType::Identifier, "column name").text;
  ColumnRef ref;
  auto dot = text.find('.');
  if (dot == std::string::npos) {
    ref.column = text;
  } else {
    ref.table  = text.substr(0, dot);
    ref.column = text.substr(dot + 1);
  }
  return ref;
}

SelectStmt Parser::parse_select() {
  SelectStmt stmt;
  expect(TokenType::Select, "SELECT");
  if (!match(TokenType::Star)) {  // empty select_list means "*"
    stmt.select_list.push_back(parse_column_ref());
    while (match(TokenType::Comma)) stmt.select_list.push_back(parse_column_ref());
  }
  expect(TokenType::From, "FROM");
  stmt.from_table = expect(TokenType::Identifier, "table name").text;

  if (match(TokenType::Join)) {
    stmt.has_join   = true;
    stmt.join_table = expect(TokenType::Identifier, "join table name").text;
    expect(TokenType::On, "ON");
    stmt.join_on = parse_expr();
  }
  if (match(TokenType::Where)) stmt.where = parse_expr();
  return stmt;
}

InsertStmt Parser::parse_insert() {
  InsertStmt stmt;
  expect(TokenType::Insert, "INSERT");
  expect(TokenType::Into, "INTO");
  stmt.table = expect(TokenType::Identifier, "table name").text;
  expect(TokenType::Values, "VALUES");
  expect(TokenType::LParen, "(");
  do {
    const Token& t = advance();
    if (t.type == TokenType::Number)      stmt.values.emplace_back(std::stoll(t.text));
    else if (t.type == TokenType::String) stmt.values.emplace_back(t.text);
    else throw std::runtime_error("parse error: expected a literal in VALUES");
  } while (match(TokenType::Comma));
  expect(TokenType::RParen, ")");
  return stmt;
}

DeleteStmt Parser::parse_delete() {
  DeleteStmt stmt;
  expect(TokenType::Delete, "DELETE");
  expect(TokenType::From, "FROM");
  stmt.table = expect(TokenType::Identifier, "table name").text;
  if (match(TokenType::Where)) stmt.where = parse_expr();
  return stmt;
}

ExprPtr Parser::parse_expr() {
  ExprPtr left = parse_and();
  while (match(TokenType::Or)) {
    auto bin = std::make_unique<BinaryExpr>();
    bin->op = "OR";
    bin->left = std::move(left);
    bin->right = parse_and();
    left = std::move(bin);
  }
  return left;
}

ExprPtr Parser::parse_and() {
  ExprPtr left = parse_comparison();
  while (match(TokenType::And)) {
    auto bin = std::make_unique<BinaryExpr>();
    bin->op = "AND";
    bin->left = std::move(left);
    bin->right = parse_comparison();
    left = std::move(bin);
  }
  return left;
}

ExprPtr Parser::parse_comparison() {
  ExprPtr left = parse_primary();
  static const std::pair<TokenType, const char*> ops[] = {
      {TokenType::Eq, "="}, {TokenType::Ne, "!="}, {TokenType::Lt, "<"},
      {TokenType::Gt, ">"}, {TokenType::Le, "<="}, {TokenType::Ge, ">="}};
  for (const auto& [type, sym] : ops) {
    if (peek().type == type) {
      advance();
      auto bin = std::make_unique<BinaryExpr>();
      bin->op = sym;
      bin->left = std::move(left);
      bin->right = parse_primary();
      return bin;
    }
  }
  return left;
}

ExprPtr Parser::parse_primary() {
  if (match(TokenType::LParen)) {
    ExprPtr e = parse_expr();
    expect(TokenType::RParen, ")");
    return e;
  }
  if (peek().type == TokenType::Number) {
    auto lit = std::make_unique<IntLit>();
    lit->value = std::stoll(advance().text);
    return lit;
  }
  if (peek().type == TokenType::String) {
    auto lit = std::make_unique<StrLit>();
    lit->value = advance().text;
    return lit;
  }
  auto ref = std::make_unique<ColumnRef>(parse_column_ref());
  return ref;
}

}  // namespace minidb
