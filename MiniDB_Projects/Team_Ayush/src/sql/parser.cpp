#include "sql/parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include "sql/lexer.h"

namespace minidb {

namespace {

std::string Upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return s;
}

// Recursive-descent parser over the token stream. Errors are thrown as
// std::runtime_error and caught by Parse().
class Parser {
 public:
  explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)), pos_(0) {}

  Statement ParseStatement() {
    Statement st;
    bool explain = false;
    if (IsKw("EXPLAIN")) { Next(); explain = true; }

    if (IsKw("CREATE")) {
      st.type = StmtType::CREATE_TABLE;
      st.create = ParseCreate();
    } else if (IsKw("INSERT")) {
      st.type = StmtType::INSERT;
      st.insert = ParseInsert();
    } else if (IsKw("SELECT")) {
      st.type = StmtType::SELECT;
      st.select = ParseSelect();
      st.select.explain = explain;
    } else if (IsKw("DELETE")) {
      st.type = StmtType::DELETE_;
      st.del = ParseDelete();
    } else {
      Fail("expected CREATE/INSERT/SELECT/DELETE");
    }
    if (Cur().type == TokType::PUNCT && Cur().text == ";") Next();
    return st;
  }

 private:
  const Token& Cur() const { return toks_[pos_]; }
  void Next() { if (pos_ + 1 < toks_.size()) ++pos_; }

  bool IsKw(const char* kw) const {
    return Cur().type == TokType::IDENT && Upper(Cur().text) == kw;
  }
  bool IsPunct(const char* s) const {
    return Cur().type == TokType::PUNCT && Cur().text == s;
  }
  void Expect(const char* p) {
    if (!IsPunct(p)) Fail(std::string("expected '") + p + "'");
    Next();
  }
  void ExpectKw(const char* kw) {
    if (!IsKw(kw)) Fail(std::string("expected ") + kw);
    Next();
  }
  std::string Ident() {
    if (Cur().type != TokType::IDENT) Fail("expected identifier");
    std::string s = Cur().text;
    Next();
    return s;
  }
  void Fail(const std::string& m) {
    throw std::runtime_error(m + " (near '" + Cur().text + "')");
  }

  // colref := ident ['.' ident]   -> (table, column)
  void ColRef(std::string* table, std::string* col) {
    std::string a = Ident();
    if (IsPunct(".")) {
      Next();
      *table = a;
      *col = Ident();
    } else {
      *table = "";
      *col = a;
    }
  }

  Value ParseValue() {
    bool neg = false;
    if (IsPunct("-")) { neg = true; Next(); }
    if (Cur().type == TokType::NUMBER) {
      int32_t v = static_cast<int32_t>(std::atoi(Cur().text.c_str()));
      Next();
      return Value::Int(neg ? -v : v);
    }
    if (Cur().type == TokType::STRING) {
      std::string s = Cur().text;
      Next();
      return Value::Varchar(s);
    }
    Fail("expected a literal value");
    return Value();  // unreachable
  }

  CompOp ParseOp() {
    if (IsPunct("=")) { Next(); return CompOp::EQ; }
    if (IsPunct("!=") || IsPunct("<>")) { Next(); return CompOp::NE; }
    if (IsPunct("<=")) { Next(); return CompOp::LE; }
    if (IsPunct(">=")) { Next(); return CompOp::GE; }
    if (IsPunct("<")) { Next(); return CompOp::LT; }
    if (IsPunct(">")) { Next(); return CompOp::GT; }
    Fail("expected comparison operator");
    return CompOp::EQ;
  }

  std::vector<Predicate> ParseWhere() {
    std::vector<Predicate> preds;
    do {
      Predicate p;
      ColRef(&p.table, &p.column);
      p.op = ParseOp();
      p.value = ParseValue();
      preds.push_back(p);
    } while (MatchKw("AND"));
    return preds;
  }

  bool MatchKw(const char* kw) {
    if (IsKw(kw)) { Next(); return true; }
    return false;
  }

  CreateTableStmt ParseCreate() {
    ExpectKw("CREATE");
    ExpectKw("TABLE");
    CreateTableStmt c;
    c.table = Ident();
    Expect("(");
    while (true) {
      // PRIMARY KEY (col)
      if (IsKw("PRIMARY")) {
        Next();
        ExpectKw("KEY");
        Expect("(");
        c.pk_column = Ident();
        Expect(")");
      } else {
        Column col;
        col.name = Ident();
        std::string ty = Upper(Ident());
        if (ty == "INT" || ty == "INTEGER") {
          col.type = ValueType::INT;
          col.length = 4;
        } else if (ty == "VARCHAR" || ty == "CHAR" || ty == "TEXT") {
          col.type = ValueType::VARCHAR;
          col.length = 32;  // default
          if (IsPunct("(")) {
            Next();
            col.length = std::atoi(Cur().text.c_str());
            Next();
            Expect(")");
          }
        } else {
          Fail("unknown type " + ty);
        }
        c.columns.push_back(col);
      }
      if (IsPunct(",")) { Next(); continue; }
      break;
    }
    Expect(")");
    return c;
  }

  InsertStmt ParseInsert() {
    ExpectKw("INSERT");
    ExpectKw("INTO");
    InsertStmt ins;
    ins.table = Ident();
    ExpectKw("VALUES");
    Expect("(");
    while (true) {
      ins.values.push_back(ParseValue());
      if (IsPunct(",")) { Next(); continue; }
      break;
    }
    Expect(")");
    return ins;
  }

  SelectStmt ParseSelect() {
    ExpectKw("SELECT");
    SelectStmt s;
    if (IsPunct("*")) {
      s.star = true;
      Next();
    } else {
      while (true) {
        std::string t, c;
        ColRef(&t, &c);
        s.select_list.push_back(t.empty() ? c : (t + "." + c));
        if (IsPunct(",")) { Next(); continue; }
        break;
      }
    }
    ExpectKw("FROM");
    s.from_table = Ident();
    if (IsKw("JOIN")) {
      Next();
      s.join.present = true;
      s.join.table = Ident();
      ExpectKw("ON");
      ColRef(&s.join.left_table, &s.join.left_col);
      Expect("=");
      ColRef(&s.join.right_table, &s.join.right_col);
    }
    if (MatchKw("WHERE")) s.where = ParseWhere();
    return s;
  }

  DeleteStmt ParseDelete() {
    ExpectKw("DELETE");
    ExpectKw("FROM");
    DeleteStmt d;
    d.table = Ident();
    if (MatchKw("WHERE")) d.where = ParseWhere();
    return d;
  }

  std::vector<Token> toks_;
  size_t pos_;
};

}  // namespace

ParseResult Parse(const std::string& sql) {
  ParseResult r;
  try {
    Parser parser(Tokenize(sql));
    r.stmt = parser.ParseStatement();
    r.ok = true;
  } catch (const std::exception& e) {
    r.ok = false;
    r.error = e.what();
  }
  return r;
}

}  // namespace minidb
