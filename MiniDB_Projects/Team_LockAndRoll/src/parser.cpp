#include "parser.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace minidb {

namespace {
const std::unordered_set<std::string> kKeywords = {
    "CREATE", "TABLE",  "INSERT",  "INTO",   "VALUES", "SELECT", "FROM",  "WHERE",
    "JOIN",   "INNER",  "ON",      "AND",    "OR",     "NOT",    "DELETE","BEGIN",
    "COMMIT", "ROLLBACK","ABORT",  "GROUP",  "BY",     "ORDER",  "ASC",   "DESC",
    "PRIMARY","KEY",    "AS",      "TRUE",   "FALSE",  "NULL",   "INTEGER","VARCHAR",
    "BOOLEAN","INT",    "TEXT",    "LIMIT",  "COUNT",  "SUM",    "AVG",   "MIN",
    "MAX"};

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
bool is_agg(const std::string& kw) {
  return kw == "COUNT" || kw == "SUM" || kw == "AVG" || kw == "MIN" || kw == "MAX";
}

// turns overflow into a clean DBException instead of letting std::out_of_range escape
int64_t to_int64(const std::string& text) {
  try {
    return std::stoll(text);
  } catch (const std::out_of_range&) {
    throw DBException("integer literal out of range: " + text);
  } catch (const std::invalid_argument&) {
    throw DBException("invalid integer literal: " + text);
  }
}
}  // namespace

std::vector<Token> Parser::lex(const std::string& sql) {
  std::vector<Token> toks;
  size_t i = 0, n = sql.size();
  while (i < n) {
    char c = sql[i];
    if (std::isspace(static_cast<unsigned char>(c))) { i++; continue; }
    // -- line comment
    if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
      while (i < n && sql[i] != '\n') i++;
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      size_t j = i;
      while (j < n && (std::isalnum(static_cast<unsigned char>(sql[j])) || sql[j] == '_')) j++;
      std::string word = sql.substr(i, j - i);
      std::string up = upper(word);
      if (kKeywords.count(up))
        toks.push_back({Token::Type::Keyword, up});
      else
        toks.push_back({Token::Type::Ident, word});
      i = j;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      size_t j = i;
      while (j < n && std::isdigit(static_cast<unsigned char>(sql[j]))) j++;
      toks.push_back({Token::Type::Number, sql.substr(i, j - i)});
      i = j;
      continue;
    }
    if (c == '\'') {  // '' is an escaped quote
      std::string s;
      i++;
      bool closed = false;
      while (i < n) {
        if (sql[i] == '\'') {
          if (i + 1 < n && sql[i + 1] == '\'') { s.push_back('\''); i += 2; continue; }
          i++;
          closed = true;
          break;
        }
        s.push_back(sql[i++]);
      }
      if (!closed) throw DBException("unterminated string literal");
      toks.push_back({Token::Type::String, s});
      continue;
    }
    auto two = (i + 1 < n) ? sql.substr(i, 2) : std::string();
    if (two == "<=" || two == ">=" || two == "!=" || two == "<>") {
      toks.push_back({Token::Type::Punct, two});
      i += 2;
      continue;
    }
    if (std::string("()*,.;=<>+-/").find(c) != std::string::npos) {
      toks.push_back({Token::Type::Punct, std::string(1, c)});
      i++;
      continue;
    }
    throw DBException(std::string("unexpected character '") + c + "' in SQL");
  }
  toks.push_back({Token::Type::End, ""});
  return toks;
}

void Parser::fail(const std::string& msg) const {
  const Token& t = toks_[pos_];
  throw DBException("parse error near '" + t.text + "': " + msg);
}
bool Parser::accept_kw(const std::string& kw) {
  if (is_kw(kw)) { pos_++; return true; }
  return false;
}
bool Parser::accept_punct(const std::string& p) {
  if (is_punct(p)) { pos_++; return true; }
  return false;
}
void Parser::expect_kw(const std::string& kw) {
  if (!accept_kw(kw)) fail("expected keyword " + kw);
}
void Parser::expect_punct(const std::string& p) {
  if (!accept_punct(p)) fail("expected '" + p + "'");
}
std::string Parser::expect_ident() {
  if (peek().type != Token::Type::Ident) fail("expected identifier");
  return next().text;
}

Statement Parser::parse(const std::string& sql) {
  Parser p(lex(sql));
  Statement s = p.parse_statement();
  p.accept_punct(";");
  if (p.peek().type != Token::Type::End) p.fail("unexpected trailing tokens");
  return s;
}

Statement Parser::parse_statement() {
  if (is_kw("CREATE")) return parse_create();
  if (is_kw("INSERT")) return parse_insert();
  if (is_kw("SELECT")) return parse_select();
  if (is_kw("DELETE")) return parse_delete();
  if (accept_kw("BEGIN")) return TxnStmt{TxnCmd::Begin};
  if (accept_kw("COMMIT")) return TxnStmt{TxnCmd::Commit};
  if (accept_kw("ROLLBACK") || accept_kw("ABORT")) return TxnStmt{TxnCmd::Rollback};
  fail("unrecognized statement");
}

CreateTableStmt Parser::parse_create() {
  expect_kw("CREATE");
  expect_kw("TABLE");
  CreateTableStmt s;
  s.table = expect_ident();
  expect_punct("(");
  do {
    ColumnDef col;
    col.name = expect_ident();
    if (accept_kw("INTEGER") || accept_kw("INT")) col.type = TypeId::INTEGER;
    else if (accept_kw("VARCHAR") || accept_kw("TEXT")) col.type = TypeId::VARCHAR;
    else if (accept_kw("BOOLEAN")) col.type = TypeId::BOOLEAN;
    else fail("expected a column type");
    // optional VARCHAR(n) length: accepted and ignored
    if (accept_punct("(")) { next(); expect_punct(")"); }
    if (accept_kw("PRIMARY")) { expect_kw("KEY"); col.primary_key = true; }
    s.columns.push_back(std::move(col));
  } while (accept_punct(","));
  expect_punct(")");
  return s;
}

InsertStmt Parser::parse_insert() {
  expect_kw("INSERT");
  expect_kw("INTO");
  InsertStmt s;
  s.table = expect_ident();
  if (accept_punct("(")) {
    do { s.columns.push_back(expect_ident()); } while (accept_punct(","));
    expect_punct(")");
  }
  expect_kw("VALUES");
  do {
    expect_punct("(");
    std::vector<ExprPtr> row;
    do { row.push_back(parse_expr()); } while (accept_punct(","));
    expect_punct(")");
    s.rows.push_back(std::move(row));
  } while (accept_punct(","));
  return s;
}

SelectStmt Parser::parse_select() {
  expect_kw("SELECT");
  SelectStmt s;
  do {
    SelectItem item;
    if (accept_punct("*")) {
      item.star = true;
    } else {
      item.expr = parse_expr();
      if (accept_kw("AS")) item.alias = expect_ident();
    }
    s.items.push_back(std::move(item));
  } while (accept_punct(","));

  expect_kw("FROM");
  s.from.name = expect_ident();
  if (peek().type == Token::Type::Ident && !is_kw("WHERE")) s.from.alias = next().text;

  while (is_kw("JOIN") || is_kw("INNER")) {
    accept_kw("INNER");
    expect_kw("JOIN");
    JoinClause jc;
    jc.right.name = expect_ident();
    if (peek().type == Token::Type::Ident && !is_kw("ON")) jc.right.alias = next().text;
    expect_kw("ON");
    jc.on = parse_expr();
    s.joins.push_back(std::move(jc));
  }

  if (accept_kw("WHERE")) s.where = parse_expr();

  if (accept_kw("GROUP")) {
    expect_kw("BY");
    do { s.group_by.push_back(parse_primary()->col_name); } while (accept_punct(","));
  }

  if (accept_kw("ORDER")) {
    expect_kw("BY");
    do {
      OrderByItem ob;
      ob.col = parse_primary()->col_name;
      if (accept_kw("DESC")) ob.desc = true;
      else accept_kw("ASC");
      s.order_by.push_back(std::move(ob));
    } while (accept_punct(","));
  }

  if (accept_kw("LIMIT")) {
    s.has_limit = true;
    if (peek().type != Token::Type::Number) fail("expected a number after LIMIT");
    s.limit = to_int64(next().text);
  }
  return s;
}

DeleteStmt Parser::parse_delete() {
  expect_kw("DELETE");
  expect_kw("FROM");
  DeleteStmt s;
  s.table = expect_ident();
  if (accept_kw("WHERE")) s.where = parse_expr();
  return s;
}

ExprPtr Parser::parse_expr() { return parse_or(); }

ExprPtr Parser::parse_or() {
  ExprPtr l = parse_and();
  while (accept_kw("OR")) {
    auto e = std::make_shared<Expr>();
    e->kind = ExprKind::Binary;
    e->op = "OR";
    e->left = l;
    e->right = parse_and();
    l = e;
  }
  return l;
}

ExprPtr Parser::parse_and() {
  ExprPtr l = parse_cmp();
  while (accept_kw("AND")) {
    auto e = std::make_shared<Expr>();
    e->kind = ExprKind::Binary;
    e->op = "AND";
    e->left = l;
    e->right = parse_cmp();
    l = e;
  }
  return l;
}

ExprPtr Parser::parse_cmp() {
  ExprPtr l = parse_add();
  for (const char* op : {"=", "!=", "<>", "<", "<=", ">", ">="}) {
    if (is_punct(op)) {
      std::string o = next().text;
      if (o == "<>") o = "!=";
      auto e = std::make_shared<Expr>();
      e->kind = ExprKind::Binary;
      e->op = o;
      e->left = l;
      e->right = parse_add();
      return e;
    }
  }
  return l;
}

ExprPtr Parser::parse_add() {
  ExprPtr l = parse_mul();
  while (is_punct("+") || is_punct("-")) {
    std::string o = next().text;
    auto e = std::make_shared<Expr>();
    e->kind = ExprKind::Binary;
    e->op = o;
    e->left = l;
    e->right = parse_mul();
    l = e;
  }
  return l;
}

ExprPtr Parser::parse_mul() {
  ExprPtr l = parse_primary();
  while (is_punct("*") || is_punct("/")) {
    std::string o = next().text;
    auto e = std::make_shared<Expr>();
    e->kind = ExprKind::Binary;
    e->op = o;
    e->left = l;
    e->right = parse_primary();
    l = e;
  }
  return l;
}

ExprPtr Parser::parse_primary() {
  auto e = std::make_shared<Expr>();
  if (accept_punct("(")) {
    ExprPtr inner = parse_expr();
    expect_punct(")");
    return inner;
  }
  if (is_punct("-")) {
    next();
    e->kind = ExprKind::Unary;
    e->op = "-";
    e->left = parse_primary();
    return e;
  }
  if (peek().type == Token::Type::Number) {
    e->kind = ExprKind::IntLit;
    e->int_val = to_int64(next().text);
    return e;
  }
  if (peek().type == Token::Type::String) {
    e->kind = ExprKind::StrLit;
    e->str_val = next().text;
    return e;
  }
  if (accept_kw("TRUE")) { e->kind = ExprKind::BoolLit; e->bool_val = true; return e; }
  if (accept_kw("FALSE")) { e->kind = ExprKind::BoolLit; e->bool_val = false; return e; }
  if (accept_kw("NULL")) { e->kind = ExprKind::NullLit; return e; }

  if (peek().type == Token::Type::Keyword && is_agg(peek().text)) {
    e->kind = ExprKind::Agg;
    e->func = next().text;
    expect_punct("(");
    if (accept_punct("*")) e->star = true;
    else e->left = parse_expr();
    expect_punct(")");
    return e;
  }

  if (peek().type == Token::Type::Ident) {
    std::string name = next().text;
    if (accept_punct(".")) name += "." + expect_ident();
    e->kind = ExprKind::Column;
    e->col_name = name;
    return e;
  }
  fail("expected an expression");
}

}  // namespace minidb
