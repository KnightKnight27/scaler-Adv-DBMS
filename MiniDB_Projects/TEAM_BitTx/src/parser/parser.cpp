#include "parser/parser.h"

#include <cstdlib>
#include <stdexcept>

namespace minidb {

using namespace std;

Parser::Parser(vector<Token> tokens) : tokens_(move(tokens)) {}

const Token& Parser::Peek() const {
  return tokens_[pos_];
}

const Token& Parser::Advance() {
  const Token& t = tokens_[pos_];
  if (t.type != TokenType::EOF_TOKEN)
    ++pos_;
  return t;
}

bool Parser::Match(TokenType t) {
  if (Check(t)) {
    Advance();
    return true;
  }
  return false;
}

bool Parser::Check(TokenType t) const {
  return Peek().type == t;
}

bool Parser::Expect(TokenType t, const char* what) {
  if (!Check(t)) {
    throw runtime_error(string("Expected ") + what + ", got " + Peek().text);
  }
  Advance();
  return true;
}

bool Parser::IsKw(const Token& t, const char* kw) const {
  return t.type == TokenType::KEYWORD && IsKeyword(t.text, kw);
}

unique_ptr<Stmt> Parser::ParseStatement() {
  if (IsKw(Peek(), "CREATE"))
    return ParseCreate();
  if (IsKw(Peek(), "DROP"))
    return ParseDrop();
  if (IsKw(Peek(), "SELECT"))
    return ParseSelect();
  if (IsKw(Peek(), "INSERT"))
    return ParseInsert();
  if (IsKw(Peek(), "DELETE"))
    return ParseDelete();
  if (IsKw(Peek(), "UPDATE"))
    return ParseUpdate();
  throw runtime_error("Unknown statement");
}

unique_ptr<Stmt> Parser::ParseCreate() {
  Advance();
  Expect(TokenType::KEYWORD, "TABLE");
  const Token& name = Peek();
  Expect(TokenType::IDENT, "table name");
  auto stmt = make_unique<CreateTableStmt>();
  stmt->tableName = name.text;
  Expect(TokenType::LPAREN, "(");
  while (!Check(TokenType::RPAREN)) {
    const Token& colName = Peek();
    Expect(TokenType::IDENT, "column name");
    ColumnDefAst col;
    col.name = colName.text;
    const Token& typeTok = Peek();
    Expect(TokenType::KEYWORD, "type");
    col.type = StringToTypeId(typeTok.text);
    if (col.type == TypeId::INVALID)
      throw runtime_error("Unknown column type");
    col.nullable = !(IsKw(Peek(), "NOT"));
    if (IsKw(Peek(), "NOT")) {
      Advance();
      Expect(TokenType::KEYWORD, "NULL");
      col.nullable = false;
    }
    if (IsKw(Peek(), "PRIMARY")) {
      Advance();
      Expect(TokenType::KEYWORD, "KEY");
      col.isPrimaryKey = true;
      col.nullable = false;
    }
    stmt->columns.push_back(col);
    if (!Match(TokenType::COMMA))
      break;
  }
  Expect(TokenType::RPAREN, ")");
  Match(TokenType::SEMICOLON);
  return stmt;
}

unique_ptr<Stmt> Parser::ParseDrop() {
  Advance();
  Expect(TokenType::KEYWORD, "TABLE");
  const Token& name = Peek();
  Expect(TokenType::IDENT, "table name");
  auto stmt = make_unique<DropTableStmt>();
  stmt->tableName = name.text;
  Match(TokenType::SEMICOLON);
  return stmt;
}

unique_ptr<Stmt> Parser::ParseSelect() {
  Advance();
  auto stmt = make_unique<SelectStmt>();
  while (!IsKw(Peek(), "FROM")) {
    if (Check(TokenType::STAR)) {
      Advance();
      break;
    }
    stmt->selectList.push_back(ParseExpr());
    if (!Match(TokenType::COMMA))
      break;
  }
  Expect(TokenType::KEYWORD, "FROM");
  const Token& name = Peek();
  Expect(TokenType::IDENT, "table name");
  stmt->fromTable = name.text;
  if (IsKw(Peek(), "WHERE")) {
    Advance();
    stmt->whereClause = ParseExpr();
  }
  if (IsKw(Peek(), "GROUP")) {
    Advance();
    Expect(TokenType::KEYWORD, "BY");
    do {
      const Token& c = Peek();
      Expect(TokenType::IDENT, "group column");
      stmt->groupBy.push_back(c.text);
    } while (Match(TokenType::COMMA));
  }
  if (IsKw(Peek(), "ORDER")) {
    Advance();
    Expect(TokenType::KEYWORD, "BY");
    do {
      const Token& c = Peek();
      Expect(TokenType::IDENT, "sort column");
      bool desc = false;
      if (IsKw(Peek(), "DESC")) {
        Advance();
        desc = true;
      } else if (IsKw(Peek(), "ASC")) {
        Advance();
      }
      stmt->orderBy.push_back({c.text, desc});
    } while (Match(TokenType::COMMA));
  }
  if (IsKw(Peek(), "HAVING")) {
    Advance();
    stmt->havingClause = ParseExpr();
  }
  if (IsKw(Peek(), "LIMIT")) {
    Advance();
    const Token& n = Peek();
    Expect(TokenType::NUMBER, "limit");
    stmt->limitCount = atoi(n.text.c_str());
  }
  Match(TokenType::SEMICOLON);
  return stmt;
}

unique_ptr<Stmt> Parser::ParseInsert() {
  Advance();
  Expect(TokenType::KEYWORD, "INTO");
  const Token& name = Peek();
  Expect(TokenType::IDENT, "table name");
  auto stmt = make_unique<InsertStmt>();
  stmt->tableName = name.text;
  if (Match(TokenType::LPAREN)) {
    while (!Check(TokenType::RPAREN)) {
      const Token& c = Peek();
      Expect(TokenType::IDENT, "column");
      stmt->columns.push_back(c.text);
      if (!Match(TokenType::COMMA))
        break;
    }
    Expect(TokenType::RPAREN, ")");
  }
  Expect(TokenType::KEYWORD, "VALUES");
  Expect(TokenType::LPAREN, "(");
  while (!Check(TokenType::RPAREN)) {
    const Token& v = Peek();
    if (v.type == TokenType::NUMBER) {
      stmt->values.push_back(Value(atoi(v.text.c_str())));
    } else if (v.type == TokenType::STRING) {
      stmt->values.push_back(Value(v.text));
    } else if (IsKw(v, "TRUE")) {
      stmt->values.push_back(Value(true));
    } else if (IsKw(v, "FALSE")) {
      stmt->values.push_back(Value(false));
    } else if (IsKw(v, "NULL")) {
      stmt->values.push_back(Value());
    }
    Advance();
    if (!Match(TokenType::COMMA))
      break;
  }
  Expect(TokenType::RPAREN, ")");
  Match(TokenType::SEMICOLON);
  return stmt;
}

unique_ptr<Stmt> Parser::ParseDelete() {
  Advance();
  Expect(TokenType::KEYWORD, "FROM");
  const Token& name = Peek();
  Expect(TokenType::IDENT, "table name");
  auto stmt = make_unique<DeleteStmt>();
  stmt->tableName = name.text;
  if (IsKw(Peek(), "WHERE")) {
    Advance();
    stmt->whereClause = ParseExpr();
  }
  Match(TokenType::SEMICOLON);
  return stmt;
}

unique_ptr<Stmt> Parser::ParseUpdate() {
  Advance();
  const Token& name = Peek();
  Expect(TokenType::IDENT, "table name");
  auto stmt = make_unique<UpdateStmt>();
  stmt->tableName = name.text;
  Expect(TokenType::KEYWORD, "SET");
  const Token& col = Peek();
  Expect(TokenType::IDENT, "column");
  stmt->setColumn = col.text;
  Expect(TokenType::PUNCT, "=");
  stmt->setValue = ParsePrimary();
  if (IsKw(Peek(), "WHERE")) {
    Advance();
    stmt->whereClause = ParseExpr();
  }
  Match(TokenType::SEMICOLON);
  return stmt;
}

unique_ptr<Expr> Parser::ParseExpr() {
  return ParseOr();
}

unique_ptr<Expr> Parser::ParseOr() {
  auto left = ParseAnd();
  while (IsKw(Peek(), "OR")) {
    Advance();
    auto right = ParseAnd();
    auto bin = make_unique<BinaryOp>();
    bin->op = "OR";
    bin->lhs = move(left);
    bin->rhs = move(right);
    left = move(bin);
  }
  return left;
}

unique_ptr<Expr> Parser::ParseAnd() {
  auto left = ParseComparison();
  while (IsKw(Peek(), "AND")) {
    Advance();
    auto right = ParseComparison();
    auto bin = make_unique<BinaryOp>();
    bin->op = "AND";
    bin->lhs = move(left);
    bin->rhs = move(right);
    left = move(bin);
  }
  return left;
}

unique_ptr<Expr> Parser::ParseComparison() {
  auto left = ParsePrimary();
  while (Check(TokenType::PUNCT)) {
    string op = Peek().text;
    if (op != "=" && op != "<" && op != ">" && op != "<=" && op != ">=" && op != "<>")
      break;
    Advance();
    auto right = ParsePrimary();
    auto bin = make_unique<BinaryOp>();
    bin->op = op;
    bin->lhs = move(left);
    bin->rhs = move(right);
    left = move(bin);
  }
  return left;
}

unique_ptr<Expr> Parser::ParsePrimary() {
  if (Match(TokenType::LPAREN)) {
    auto e = ParseExpr();
    Expect(TokenType::RPAREN, ")");
    return e;
  }
  return ParseColumnRefOrLiteral();
}

unique_ptr<Expr> Parser::ParseColumnRefOrLiteral() {
  const Token& t = Peek();
  if (t.type == TokenType::NUMBER) {
    Advance();
    auto lit = make_unique<Literal>();
    lit->v = Value(atoi(t.text.c_str()));
    return lit;
  }
  if (t.type == TokenType::STRING) {
    Advance();
    auto lit = make_unique<Literal>();
    lit->v = Value(t.text);
    return lit;
  }
  if (IsKw(t, "TRUE")) {
    Advance();
    auto lit = make_unique<Literal>();
    lit->v = Value(true);
    return lit;
  }
  if (IsKw(t, "FALSE")) {
    Advance();
    auto lit = make_unique<Literal>();
    lit->v = Value(false);
    return lit;
  }
  if (IsKw(t, "NULL")) {
    Advance();
    auto lit = make_unique<Literal>();
    lit->v = Value();
    return lit;
  }
  if (t.type == TokenType::IDENT) {
    Advance();
    auto ref = make_unique<ColumnRef>();
    size_t dot = t.text.find('.');
    if (dot != string::npos) {
      ref->tableName = t.text.substr(0, dot);
      ref->columnName = t.text.substr(dot + 1);
    } else {
      ref->columnName = t.text;
    }
    return ref;
  }
  throw runtime_error("Unexpected token in expression: " + t.text);
}

} // namespace minidb