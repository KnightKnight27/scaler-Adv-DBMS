#include "sql/parser.h"
#include <cctype>
#include <unordered_map>

namespace minidb {

Parser::Parser(const std::string& sql) { tokens_ = Tokenize(sql); }

std::vector<Token> Parser::Tokenize(const std::string& sql) {
  static const std::unordered_map<std::string, Tok> kw = {
      {"CREATE", Tok::Create}, {"TABLE", Tok::Table}, {"INSERT", Tok::Insert},
      {"INTO", Tok::Into},     {"VALUES", Tok::Values}, {"SELECT", Tok::Select},
      {"FROM", Tok::From},     {"WHERE", Tok::Where}, {"JOIN", Tok::Join},
      {"ON", Tok::On},         {"DELETE", Tok::Delete}, {"AND", Tok::And},
      {"INTEGER", Tok::KwInteger}, {"INT", Tok::KwInteger},
      {"VARCHAR", Tok::KwVarchar}, {"TEXT", Tok::KwVarchar},
      {"COUNT", Tok::Count},
      {"BEGIN", Tok::Begin}, {"COMMIT", Tok::Commit},
      {"ABORT", Tok::Abort}, {"ROLLBACK", Tok::Rollback}, {"USING", Tok::Using}};

  std::vector<Token> out;
  size_t i = 0, n = sql.size();
  while (i < n) {
    char c = sql[i];
    if (std::isspace(static_cast<unsigned char>(c))) { i++; continue; }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      size_t j = i;
      while (j < n && (std::isalnum(static_cast<unsigned char>(sql[j])) || sql[j] == '_')) j++;
      std::string word = sql.substr(i, j - i);
      std::string up = word;
      for (auto& ch : up) ch = static_cast<char>(std::toupper(ch));
      i = j;
      // PRIMARY KEY is two keywords; collapse to one token.
      if (up == "PRIMARY") {
        // skip whitespace then expect KEY
        size_t k = i;
        while (k < n && std::isspace(static_cast<unsigned char>(sql[k]))) k++;
        if (sql.compare(k, 3, "KEY") == 0 || sql.compare(k, 3, "key") == 0 ||
            sql.compare(k, 3, "Key") == 0) {
          i = k + 3;
          out.push_back({Tok::PrimaryKey, "", 0});
          continue;
        }
      }
      if (up == "KEY") { /* stray KEY -- treat as ident */ }
      auto it = kw.find(up);
      if (it != kw.end()) out.push_back({it->second, word, 0});
      else out.push_back({Tok::Ident, word, 0});
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
      size_t j = i + (c == '-' ? 1 : 0);
      while (j < n && std::isdigit(static_cast<unsigned char>(sql[j]))) j++;
      Token t{Tok::IntLit, "", 0};
      t.int_val = std::stoll(sql.substr(i, j - i));
      out.push_back(t);
      i = j;
      continue;
    }

    if (c == '\'') {
      size_t j = i + 1;
      std::string s;
      while (j < n && sql[j] != '\'') s.push_back(sql[j++]);
      if (j >= n) throw DBError("unterminated string literal");
      out.push_back({Tok::StrLit, s, 0});
      i = j + 1;
      continue;
    }

    auto two = [&](char a, char b) { return c == a && i + 1 < n && sql[i + 1] == b; };
    if (two('!', '=')) { out.push_back({Tok::Ne, "", 0}); i += 2; continue; }
    if (two('<', '=')) { out.push_back({Tok::Le, "", 0}); i += 2; continue; }
    if (two('>', '=')) { out.push_back({Tok::Ge, "", 0}); i += 2; continue; }
    if (two('<', '>')) { out.push_back({Tok::Ne, "", 0}); i += 2; continue; }

    switch (c) {
      case '(': out.push_back({Tok::LParen, "", 0}); break;
      case ')': out.push_back({Tok::RParen, "", 0}); break;
      case ',': out.push_back({Tok::Comma, "", 0}); break;
      case ';': out.push_back({Tok::Semicolon, "", 0}); break;
      case '*': out.push_back({Tok::Star, "", 0}); break;
      case '.': out.push_back({Tok::Dot, "", 0}); break;
      case '=': out.push_back({Tok::Eq, "", 0}); break;
      case '<': out.push_back({Tok::Lt, "", 0}); break;
      case '>': out.push_back({Tok::Gt, "", 0}); break;
      default: throw DBError(std::string("unexpected character: ") + c);
    }
    i++;
  }
  out.push_back({Tok::End, "", 0});
  return out;
}

bool Parser::Match(Tok k) {
  if (Check(k)) { pos_++; return true; }
  return false;
}

const Token& Parser::Expect(Tok k, const char* what) {
  if (!Check(k)) throw DBError(std::string("syntax error: expected ") + what);
  return Advance();
}

Statement Parser::Parse() {
  Statement s;
  switch (Peek().kind) {
    case Tok::Create: s = ParseCreate(); break;
    case Tok::Insert: s = ParseInsert(); break;
    case Tok::Select: s = ParseSelect(); break;
    case Tok::Delete: s = ParseDelete(); break;
    case Tok::Begin:    Advance(); s.type = StmtType::Begin; break;
    case Tok::Commit:   Advance(); s.type = StmtType::Commit; break;
    case Tok::Abort:    Advance(); s.type = StmtType::Abort; break;
    case Tok::Rollback: Advance(); s.type = StmtType::Abort; break;
    default: throw DBError("syntax error: unknown statement");
  }
  Match(Tok::Semicolon);
  return s;
}

Statement Parser::ParseCreate() {
  Statement s;
  s.type = StmtType::CreateTable;
  Expect(Tok::Create, "CREATE");
  Expect(Tok::Table, "TABLE");
  s.table = Expect(Tok::Ident, "table name").text;
  Expect(Tok::LParen, "(");
  while (true) {
    ColumnDef col;
    col.name = Expect(Tok::Ident, "column name").text;
    if (Match(Tok::KwInteger)) col.type = TypeId::INTEGER;
    else if (Match(Tok::KwVarchar)) {
      col.type = TypeId::VARCHAR;
      // optional (length) -- parsed and ignored
      if (Match(Tok::LParen)) { Expect(Tok::IntLit, "length"); Expect(Tok::RParen, ")"); }
    } else throw DBError("syntax error: expected column type");
    if (Match(Tok::PrimaryKey)) col.primary_key = true;
    s.columns.push_back(col);
    if (!Match(Tok::Comma)) break;
  }
  Expect(Tok::RParen, ")");
  // Optional storage-engine selector: USING HEAP | LSM (default HEAP).
  if (Match(Tok::Using)) {
    std::string eng = Expect(Tok::Ident, "engine name").text;
    for (auto& ch : eng) ch = static_cast<char>(std::toupper(ch));
    if (eng == "LSM") s.engine = EngineType::LSM;
    else if (eng == "HEAP") s.engine = EngineType::Heap;
    else throw DBError("unknown storage engine: " + eng + " (use HEAP or LSM)");
  }
  return s;
}

Statement Parser::ParseInsert() {
  Statement s;
  s.type = StmtType::Insert;
  Expect(Tok::Insert, "INSERT");
  Expect(Tok::Into, "INTO");
  s.table = Expect(Tok::Ident, "table name").text;
  Expect(Tok::Values, "VALUES");
  Expect(Tok::LParen, "(");
  while (true) {
    s.insert_values.push_back(ParseLiteral());
    if (!Match(Tok::Comma)) break;
  }
  Expect(Tok::RParen, ")");
  return s;
}

Value Parser::ParseLiteral() {
  if (Check(Tok::IntLit)) return Value::Int(Advance().int_val);
  if (Check(Tok::StrLit)) return Value::Str(Advance().text);
  throw DBError("syntax error: expected literal value");
}

ColRef Parser::ParseColRef() {
  ColRef ref;
  std::string first = Expect(Tok::Ident, "column").text;
  if (Match(Tok::Dot)) {
    ref.table = first;
    ref.column = Expect(Tok::Ident, "column").text;
  } else {
    ref.column = first;
  }
  return ref;
}

Predicate Parser::ParsePredicate() {
  Predicate p;
  p.lhs = ParseColRef();
  switch (Peek().kind) {
    case Tok::Eq: p.op = CmpOp::EQ; break;
    case Tok::Ne: p.op = CmpOp::NE; break;
    case Tok::Lt: p.op = CmpOp::LT; break;
    case Tok::Le: p.op = CmpOp::LE; break;
    case Tok::Gt: p.op = CmpOp::GT; break;
    case Tok::Ge: p.op = CmpOp::GE; break;
    default: throw DBError("syntax error: expected comparison operator");
  }
  Advance();
  if (Check(Tok::Ident)) {
    p.rhs_is_col = true;
    p.rhs_col = ParseColRef();
  } else {
    p.rhs_val = ParseLiteral();
  }
  return p;
}

std::vector<Predicate> Parser::ParseWhere() {
  std::vector<Predicate> preds;
  if (Match(Tok::Where)) {
    preds.push_back(ParsePredicate());
    while (Match(Tok::And)) preds.push_back(ParsePredicate());
  }
  return preds;
}

Statement Parser::ParseSelect() {
  Statement s;
  s.type = StmtType::Select;
  Expect(Tok::Select, "SELECT");
  if (Match(Tok::Count)) {
    Expect(Tok::LParen, "(");
    Expect(Tok::Star, "*");
    Expect(Tok::RParen, ")");
    s.count_star = true;
  } else if (Match(Tok::Star)) {
    s.select_star = true;
  } else {
    s.select_columns.push_back(ParseColRef());
    while (Match(Tok::Comma)) s.select_columns.push_back(ParseColRef());
  }
  Expect(Tok::From, "FROM");
  s.table = Expect(Tok::Ident, "table name").text;
  if (Match(Tok::Join)) {
    s.join.present = true;
    s.join.table = Expect(Tok::Ident, "join table").text;
    Expect(Tok::On, "ON");
    s.join.on = ParsePredicate();
  }
  s.where = ParseWhere();
  return s;
}

Statement Parser::ParseDelete() {
  Statement s;
  s.type = StmtType::Delete;
  Expect(Tok::Delete, "DELETE");
  Expect(Tok::From, "FROM");
  s.table = Expect(Tok::Ident, "table name").text;
  s.where = ParseWhere();
  return s;
}

}  // namespace minidb
