#include "sql/parser.h"

namespace minidb {

// Uppercase a string for case-insensitive keyword matching.
static string upper(const string &s) {
  string r = s;
  for (char &c : r) c = toupper((unsigned char)c);
  return r;
}

// ----------------------------- cursor helpers ------------------------------
bool Parser::isKeyword(const string &kw) const {
  return peek().kind == TokKind::kIdent && upper(peek().text) == kw;
}
bool Parser::acceptKeyword(const string &kw) {
  if (isKeyword(kw)) { ++pos_; return true; }
  return false;
}
void Parser::expectKeyword(const string &kw) {
  if (!acceptKeyword(kw)) throw ParseError("expected '" + kw + "' but got '" + peek().text + "'");
}
bool Parser::isSymbol(const string &s) const {
  return peek().kind == TokKind::kSymbol && peek().text == s;
}
bool Parser::acceptSymbol(const string &s) {
  if (isSymbol(s)) { ++pos_; return true; }
  return false;
}
void Parser::expectSymbol(const string &s) {
  if (!acceptSymbol(s)) throw ParseError("expected '" + s + "' but got '" + peek().text + "'");
}
string Parser::expectIdent() {
  if (peek().kind != TokKind::kIdent) throw ParseError("expected a name but got '" + peek().text + "'");
  return next().text;
}

// -------------------------------- entry ------------------------------------
unique_ptr<Statement> Parser::parse(const string &sql) {
  Parser p(Lexer(sql).tokenize());
  auto stmt = p.parseStatement();
  p.acceptSymbol(";");
  if (p.peek().kind != TokKind::kEnd) throw ParseError("unexpected trailing input: '" + p.peek().text + "'");
  return stmt;
}

unique_ptr<Statement> Parser::parseStatement() {
  if (isKeyword("CREATE")) return parseCreate();
  if (isKeyword("INSERT")) return parseInsert();
  if (isKeyword("SELECT")) return parseSelect();
  if (isKeyword("DELETE")) return parseDelete();
  if (acceptKeyword("BEGIN"))  return make_unique<TxnStmt>(StmtType::kBegin);
  if (acceptKeyword("COMMIT")) return make_unique<TxnStmt>(StmtType::kCommit);
  if (acceptKeyword("ROLLBACK") || acceptKeyword("ABORT"))
    return make_unique<TxnStmt>(StmtType::kAbort);
  throw ParseError("unknown statement starting with '" + peek().text + "'");
}

// ------------------------------- CREATE ------------------------------------
unique_ptr<Statement> Parser::parseCreate() {
  expectKeyword("CREATE");
  if (acceptKeyword("TABLE")) {
    auto s = make_unique<CreateTableStmt>();
    s->table = expectIdent();
    expectSymbol("(");
    do {
      ColumnDef col;
      col.name = expectIdent();
      // type
      if (acceptKeyword("INT") || acceptKeyword("INTEGER")) col.type = TypeId::INTEGER;
      else if (acceptKeyword("VARCHAR") || acceptKeyword("TEXT")) {
        col.type = TypeId::VARCHAR;
        if (acceptSymbol("(")) { parseValue(); expectSymbol(")"); }  // ignore length
      } else throw ParseError("expected a column type for '" + col.name + "'");
      // optional PRIMARY KEY
      if (acceptKeyword("PRIMARY")) { expectKeyword("KEY"); col.primary_key = true; }
      s->columns.push_back(col);
    } while (acceptSymbol(","));
    expectSymbol(")");
    return s;
  }
  if (acceptKeyword("INDEX")) {
    auto s = make_unique<CreateIndexStmt>();
    s->index_name = expectIdent();
    expectKeyword("ON");
    s->table = expectIdent();
    expectSymbol("(");
    s->column = expectIdent();
    expectSymbol(")");
    return s;
  }
  throw ParseError("expected TABLE or INDEX after CREATE");
}

// ------------------------------- INSERT ------------------------------------
unique_ptr<Statement> Parser::parseInsert() {
  expectKeyword("INSERT");
  expectKeyword("INTO");
  auto s = make_unique<InsertStmt>();
  s->table = expectIdent();
  expectKeyword("VALUES");
  expectSymbol("(");
  do { s->values.push_back(parseValue()); } while (acceptSymbol(","));
  expectSymbol(")");
  return s;
}

// ------------------------------- SELECT ------------------------------------
unique_ptr<Statement> Parser::parseSelect() {
  expectKeyword("SELECT");
  auto s = make_unique<SelectStmt>();
  if (acceptSymbol("*")) {
    s->star = true;
  } else {
    do { s->columns.push_back(parseColRef()); } while (acceptSymbol(","));
  }
  expectKeyword("FROM");
  s->table = expectIdent();

  if (acceptKeyword("JOIN")) {
    s->has_join = true;
    s->join_table = expectIdent();
    expectKeyword("ON");
    auto l = parseColRef();
    expectSymbol("=");
    auto r = parseColRef();
    s->join_left_table  = l.first;  s->join_left_col  = l.second;
    s->join_right_table = r.first;  s->join_right_col = r.second;
  }

  s->where = parseWhere();
  return s;
}

// ------------------------------- DELETE ------------------------------------
unique_ptr<Statement> Parser::parseDelete() {
  expectKeyword("DELETE");
  expectKeyword("FROM");
  auto s = make_unique<DeleteStmt>();
  s->table = expectIdent();
  s->where = parseWhere();
  return s;
}

// ------------------------------- shared ------------------------------------
vector<Predicate> Parser::parseWhere() {
  vector<Predicate> preds;
  if (acceptKeyword("WHERE")) {
    do { preds.push_back(parsePredicate()); } while (acceptKeyword("AND"));
  }
  return preds;
}

Predicate Parser::parsePredicate() {
  Predicate p;
  auto col = parseColRef();
  p.table = col.first;
  p.column = col.second;
  // operator
  if (acceptSymbol("=")) p.op = CompOp::kEq;
  else if (acceptSymbol("!=") || acceptSymbol("<>")) p.op = CompOp::kNe;
  else if (acceptSymbol("<=")) p.op = CompOp::kLe;
  else if (acceptSymbol(">=")) p.op = CompOp::kGe;
  else if (acceptSymbol("<")) p.op = CompOp::kLt;
  else if (acceptSymbol(">")) p.op = CompOp::kGt;
  else throw ParseError("expected a comparison operator but got '" + peek().text + "'");
  p.value = parseValue();
  return p;
}

pair<string, string> Parser::parseColRef() {
  string first = expectIdent();
  if (acceptSymbol(".")) {            // qualified: table.column
    string col = expectIdent();
    return {first, col};
  }
  return {"", first};                 // unqualified column
}

Value Parser::parseValue() {
  const Token &t = peek();
  if (t.kind == TokKind::kNumber) { next(); return Value(stoi(t.text)); }
  if (t.kind == TokKind::kString) { next(); return Value(t.text); }
  throw ParseError("expected a value (number or 'string') but got '" + t.text + "'");
}

}  // namespace minidb
