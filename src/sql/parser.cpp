#include "common/exception.h"
#include "sql/sql.h"

namespace minidb {

StmtPtr Parser::Parse(const std::string &sql) {
  Lexer lexer(sql);
  Parser parser(lexer.Tokenize());
  StmtPtr stmt = parser.ParseStatement();
  parser.Accept(";");
  if (!parser.AtEnd()) {
    throw Exception(ErrorKind::kParse, "trailing tokens after statement near '" + parser.Peek().text + "'");
  }
  return stmt;
}

bool Parser::Check(const std::string &text) const {
  const Token &t = Peek();
  return (t.type == TokenType::kKeyword || t.type == TokenType::kPunct) && t.text == text;
}

bool Parser::Accept(const std::string &text) {
  if (Check(text)) {
    pos_++;
    return true;
  }
  return false;
}

void Parser::Expect(const std::string &text) {
  if (!Accept(text)) {
    throw Exception(ErrorKind::kParse, "expected '" + text + "' but found '" + Peek().text + "'");
  }
}

StmtPtr Parser::ParseStatement() {
  const Token &t = Peek();
  if (t.type != TokenType::kKeyword) {
    throw Exception(ErrorKind::kParse, "expected a statement keyword, found '" + t.text + "'");
  }
  if (t.text == "CREATE") return ParseCreate();
  if (t.text == "INSERT") return ParseInsert();
  if (t.text == "SELECT") return ParseSelect();
  if (t.text == "DELETE") return ParseDelete();
  if (t.text == "BEGIN" || t.text == "COMMIT" || t.text == "ROLLBACK") {
    Next();
    auto stmt = std::make_unique<Statement>();
    stmt->type = t.text == "BEGIN"    ? StmtType::kBegin
                 : t.text == "COMMIT" ? StmtType::kCommit
                                      : StmtType::kRollback;
    return stmt;
  }
  throw Exception(ErrorKind::kParse, "unsupported statement '" + t.text + "'");
}

static std::string ExpectName(Parser *, const Token &t) {
  if (t.type != TokenType::kIdentifier) {
    throw Exception(ErrorKind::kParse, "expected an identifier, found '" + t.text + "'");
  }
  return t.text;
}

StmtPtr Parser::ParseCreate() {
  Expect("CREATE");
  auto stmt = std::make_unique<Statement>();
  if (Accept("TABLE")) {
    stmt->type = StmtType::kCreateTable;
    stmt->table = ExpectName(this, Next());
    Expect("(");
    do {
      ColumnDef col;
      col.name = ExpectName(this, Next());
      const Token &type_tok = Next();
      if (type_tok.text == "INT" || type_tok.text == "INTEGER") {
        col.type = TypeId::INTEGER;
        col.length = 4;
      } else if (type_tok.text == "VARCHAR" || type_tok.text == "TEXT") {
        col.type = TypeId::VARCHAR;
        col.length = 255;
        if (Accept("(")) {  // optional VARCHAR(n)
          col.length = static_cast<uint32_t>(std::stoi(Next().text));
          Expect(")");
        }
      } else {
        throw Exception(ErrorKind::kParse, "unknown column type '" + type_tok.text + "'");
      }
      stmt->columns.push_back(col);
    } while (Accept(","));
    Expect(")");
    return stmt;
  }
  if (Accept("INDEX")) {
    stmt->type = StmtType::kCreateIndex;
    stmt->index_name = ExpectName(this, Next());
    Expect("ON");
    stmt->table = ExpectName(this, Next());
    Expect("(");
    stmt->index_column = ExpectName(this, Next());
    Expect(")");
    return stmt;
  }
  throw Exception(ErrorKind::kParse, "expected TABLE or INDEX after CREATE");
}

StmtPtr Parser::ParseInsert() {
  Expect("INSERT");
  Expect("INTO");
  auto stmt = std::make_unique<Statement>();
  stmt->type = StmtType::kInsert;
  stmt->table = ExpectName(this, Next());
  Expect("VALUES");
  do {
    Expect("(");
    std::vector<Value> row;
    do {
      row.push_back(ParseLiteral());
    } while (Accept(","));
    Expect(")");
    stmt->rows.push_back(std::move(row));
  } while (Accept(","));
  return stmt;
}

StmtPtr Parser::ParseSelect() {
  Expect("SELECT");
  auto stmt = std::make_unique<Statement>();
  stmt->type = StmtType::kSelect;
  if (Accept("*")) {
    stmt->select_star = true;
  } else {
    do {
      // column name, optionally qualified table.col
      std::string first = ExpectName(this, Next());
      if (Accept(".")) {
        std::string col = ExpectName(this, Next());
        stmt->select_list.push_back(first + "." + col);
      } else {
        stmt->select_list.push_back(first);
      }
    } while (Accept(","));
  }
  Expect("FROM");
  stmt->from_table = ExpectName(this, Next());
  Accept("INNER");
  if (Accept("JOIN")) {
    stmt->has_join = true;
    stmt->join_table = ExpectName(this, Next());
    Expect("ON");
    stmt->join_on = ParseExpr();
  }
  if (Accept("WHERE")) {
    stmt->where = ParseExpr();
  }
  return stmt;
}

StmtPtr Parser::ParseDelete() {
  Expect("DELETE");
  Expect("FROM");
  auto stmt = std::make_unique<Statement>();
  stmt->type = StmtType::kDelete;
  stmt->table = ExpectName(this, Next());
  if (Accept("WHERE")) {
    stmt->where = ParseExpr();
  }
  return stmt;
}

// ---- expression grammar: OR > AND > comparison > primary -------------------
ExprPtr Parser::ParseExpr() {
  ExprPtr left = ParseAndExpr();
  while (Accept("OR")) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::kBinary;
    e->op = BinOp::kOr;
    e->left = std::move(left);
    e->right = ParseAndExpr();
    left = std::move(e);
  }
  return left;
}

ExprPtr Parser::ParseAndExpr() {
  ExprPtr left = ParseComparison();
  while (Accept("AND")) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::kBinary;
    e->op = BinOp::kAnd;
    e->left = std::move(left);
    e->right = ParseComparison();
    left = std::move(e);
  }
  return left;
}

ExprPtr Parser::ParseComparison() {
  ExprPtr left = ParsePrimary();
  static const std::pair<const char *, BinOp> ops[] = {
      {"=", BinOp::kEq},  {"!=", BinOp::kNe}, {"<>", BinOp::kNe}, {"<=", BinOp::kLe},
      {">=", BinOp::kGe}, {"<", BinOp::kLt},  {">", BinOp::kGt}};
  for (auto &[txt, op] : ops) {
    if (Accept(txt)) {
      auto e = std::make_unique<Expr>();
      e->kind = ExprKind::kBinary;
      e->op = op;
      e->left = std::move(left);
      e->right = ParsePrimary();
      return e;
    }
  }
  return left;  // bare column/const (rare; treated as truthy predicate)
}

ExprPtr Parser::ParsePrimary() {
  if (Accept("(")) {
    ExprPtr e = ParseExpr();
    Expect(")");
    return e;
  }
  const Token &t = Peek();
  if (t.type == TokenType::kNumber || t.type == TokenType::kString) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::kConst;
    e->value = ParseLiteral();
    return e;
  }
  if (t.type == TokenType::kIdentifier) {
    auto e = std::make_unique<Expr>();
    e->kind = ExprKind::kColumn;
    std::string first = Next().text;
    if (Accept(".")) {
      e->col_table = first;
      e->col_name = ExpectName(this, Next());
    } else {
      e->col_name = first;
    }
    return e;
  }
  throw Exception(ErrorKind::kParse, "expected an expression, found '" + t.text + "'");
}

Value Parser::ParseLiteral() {
  const Token &t = Next();
  if (t.type == TokenType::kNumber) return Value(static_cast<int32_t>(std::stoi(t.text)));
  if (t.type == TokenType::kString) return Value(t.text);
  throw Exception(ErrorKind::kParse, "expected a literal, found '" + t.text + "'");
}

}  // namespace minidb
