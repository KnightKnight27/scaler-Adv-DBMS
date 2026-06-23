#include "sql/parser.h"
#include <cctype>
#include <algorithm>

namespace minidb {

static std::string Upper(const std::string &s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(), ::toupper);
  return r;
}

void Parser::Tokenize(const std::string &sql) {
  tokens_.clear();
  pos_ = 0;
  size_t i = 0, n = sql.size();
  while (i < n) {
    char c = sql[i];
    if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

    // SQL line comment: -- ... to end of line.
    if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
      while (i < n && sql[i] != '\n') ++i;
      continue;
    }

    // Identifier / keyword (allow '.' so qualified names like t.a are one token).
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      size_t start = i;
      while (i < n && (std::isalnum(static_cast<unsigned char>(sql[i])) ||
                       sql[i] == '_' || sql[i] == '.')) ++i;
      tokens_.push_back({TokType::IDENT, sql.substr(start, i - start)});
      continue;
    }
    // Number.
    if (std::isdigit(static_cast<unsigned char>(c))) {
      size_t start = i;
      while (i < n && std::isdigit(static_cast<unsigned char>(sql[i]))) ++i;
      tokens_.push_back({TokType::NUMBER, sql.substr(start, i - start)});
      continue;
    }
    // String literal in single quotes.
    if (c == '\'') {
      ++i;
      std::string s;
      while (i < n && sql[i] != '\'') s.push_back(sql[i++]);
      if (i >= n) throw ParseError("unterminated string literal");
      ++i;  // closing quote
      tokens_.push_back({TokType::STRING, s});
      continue;
    }
    // Multi-character operators.
    if ((c == '<' || c == '>' || c == '!' || c == '=') && i + 1 < n && sql[i + 1] == '=') {
      tokens_.push_back({TokType::PUNCT, sql.substr(i, 2)});
      i += 2;
      continue;
    }
    // Single-character punctuation.
    tokens_.push_back({TokType::PUNCT, std::string(1, c)});
    ++i;
  }
  tokens_.push_back({TokType::END, ""});
}

bool Parser::IsKeyword(const std::string &kw) const {
  return Peek().type == TokType::IDENT && Upper(Peek().text) == kw;
}
bool Parser::AcceptKeyword(const std::string &kw) {
  if (IsKeyword(kw)) { ++pos_; return true; }
  return false;
}
void Parser::ExpectKeyword(const std::string &kw) {
  if (!AcceptKeyword(kw)) throw ParseError("expected keyword '" + kw + "' near '" + Peek().text + "'");
}
bool Parser::AcceptPunct(const std::string &p) {
  if (Peek().type == TokType::PUNCT && Peek().text == p) { ++pos_; return true; }
  return false;
}
void Parser::ExpectPunct(const std::string &p) {
  if (!AcceptPunct(p)) throw ParseError("expected '" + p + "' near '" + Peek().text + "'");
}
std::string Parser::ExpectIdent() {
  if (Peek().type != TokType::IDENT) throw ParseError("expected identifier near '" + Peek().text + "'");
  return Next().text;
}

Value Parser::ParseLiteral() {
  bool neg = AcceptPunct("-");
  if (Peek().type == TokType::NUMBER) {
    int64_t v = std::stoll(Next().text);
    return Value::Int(neg ? -v : v);
  }
  if (Peek().type == TokType::STRING) {
    if (neg) throw ParseError("unary '-' before string literal");
    return Value::Str(Next().text);
  }
  throw ParseError("expected literal near '" + Peek().text + "'");
}

std::vector<Predicate> Parser::ParseWhere() {
  std::vector<Predicate> preds;
  do {
    Predicate p;
    p.column = ExpectIdent();
    if      (AcceptPunct("="))  p.op = CompareOp::EQ;
    else if (AcceptPunct("!=")) p.op = CompareOp::NE;
    else if (AcceptPunct("<=")) p.op = CompareOp::LE;
    else if (AcceptPunct(">=")) p.op = CompareOp::GE;
    else if (AcceptPunct("<"))  p.op = CompareOp::LT;
    else if (AcceptPunct(">"))  p.op = CompareOp::GT;
    else throw ParseError("expected comparison operator near '" + Peek().text + "'");
    p.value = ParseLiteral();
    preds.push_back(p);
  } while (AcceptKeyword("AND"));
  return preds;
}

Statement Parser::ParseCreate() {
  ExpectKeyword("TABLE");
  Statement st;
  st.type = StmtType::CREATE_TABLE;
  st.create.table = ExpectIdent();
  ExpectPunct("(");
  do {
    Column col;
    col.name = ExpectIdent();
    std::string ty = Upper(ExpectIdent());
    if (ty == "INT" || ty == "INTEGER") col.type = TypeId::INTEGER;
    else if (ty == "VARCHAR" || ty == "TEXT" || ty == "STRING") {
      col.type = TypeId::VARCHAR;
      if (AcceptPunct("(")) { Next(); ExpectPunct(")"); }  // ignore length
    } else throw ParseError("unknown type '" + ty + "'");
    if (AcceptKeyword("PRIMARY")) { ExpectKeyword("KEY"); col.is_primary_key = true; }
    st.create.columns.push_back(col);
  } while (AcceptPunct(","));
  ExpectPunct(")");
  if (AcceptKeyword("USING")) {
    std::string eng = Upper(ExpectIdent());
    st.create.storage = (eng == "LSM") ? StorageType::LSM : StorageType::HEAP;
  }
  AcceptPunct(";");
  return st;
}

Statement Parser::ParseInsert() {
  ExpectKeyword("INTO");
  Statement st;
  st.type = StmtType::INSERT;
  st.insert.table = ExpectIdent();
  ExpectKeyword("VALUES");
  ExpectPunct("(");
  do { st.insert.values.push_back(ParseLiteral()); } while (AcceptPunct(","));
  ExpectPunct(")");
  AcceptPunct(";");
  return st;
}

Statement Parser::ParseSelect() {
  Statement st;
  st.type = StmtType::SELECT;
  if (AcceptKeyword("COUNT")) {
    ExpectPunct("(");
    ExpectPunct("*");
    ExpectPunct(")");
    st.select.count_star = true;
  } else if (AcceptPunct("*")) {
    st.select.star = true;
  } else {
    do { st.select.columns.push_back(ExpectIdent()); } while (AcceptPunct(","));
  }
  ExpectKeyword("FROM");
  st.select.table = ExpectIdent();
  if (AcceptKeyword("JOIN")) {
    st.select.join.present = true;
    st.select.join.table = ExpectIdent();
    ExpectKeyword("ON");
    st.select.join.left_col = ExpectIdent();
    ExpectPunct("=");
    st.select.join.right_col = ExpectIdent();
  }
  if (AcceptKeyword("WHERE")) st.select.where = ParseWhere();
  AcceptPunct(";");
  return st;
}

Statement Parser::ParseDelete() {
  ExpectKeyword("FROM");
  Statement st;
  st.type = StmtType::DELETE;
  st.del.table = ExpectIdent();
  if (AcceptKeyword("WHERE")) st.del.where = ParseWhere();
  AcceptPunct(";");
  return st;
}

Statement Parser::Parse(const std::string &sql) {
  Tokenize(sql);
  if (AcceptKeyword("CREATE"))   return ParseCreate();
  if (AcceptKeyword("INSERT"))   return ParseInsert();
  if (AcceptKeyword("SELECT"))   return ParseSelect();
  if (AcceptKeyword("DELETE"))   return ParseDelete();
  if (AcceptKeyword("BEGIN"))    { AcceptPunct(";"); Statement s; s.type = StmtType::BEGIN; return s; }
  if (AcceptKeyword("COMMIT"))   { AcceptPunct(";"); Statement s; s.type = StmtType::COMMIT; return s; }
  if (AcceptKeyword("ROLLBACK")) { AcceptPunct(";"); Statement s; s.type = StmtType::ROLLBACK; return s; }
  throw ParseError("unrecognized statement near '" + Peek().text + "'");
}

}  // namespace minidb
