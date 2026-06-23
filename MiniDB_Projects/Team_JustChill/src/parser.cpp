// parser.cpp — Track 3 (Query & Concurrency): SQL front-end
#include "parser.h"

#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace minidb {
namespace {

// The keyword set. Anything matching one of these (case-insensitively) is
// lexed as a Keyword token with its upper-cased spelling; everything else of
// the same shape is an identifier.
const std::unordered_set<std::string>& keywords() {
  static const std::unordered_set<std::string> kw = {
      "SELECT", "FROM",   "WHERE",  "INSERT", "INTO", "VALUES",
      "DELETE", "JOIN",   "ON",     "EXPLAIN", "AND",  "OR"};
  return kw;
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

[[noreturn]] void fail(const std::string& msg) {
  throw std::runtime_error("SQL parse error: " + msg);
}

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

}  // namespace

// ---- Lexer ----

std::vector<Token> tokenize(const std::string& sql) {
  std::vector<Token> out;
  size_t i = 0, n = sql.size();

  while (i < n) {
    char c = sql[i];

    // Whitespace.
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }

    // Punctuation / single-char tokens.
    switch (c) {
      case ',': out.push_back({TokenType::Comma, ",", 0}); ++i; continue;
      case '.': out.push_back({TokenType::Dot, ".", 0}); ++i; continue;
      case '(': out.push_back({TokenType::LParen, "(", 0}); ++i; continue;
      case ')': out.push_back({TokenType::RParen, ")", 0}); ++i; continue;
      case '*': out.push_back({TokenType::Star, "*", 0}); ++i; continue;
      default: break;
    }

    // Comparison operators: = != <> < <= > >=
    if (c == '=') {
      out.push_back({TokenType::Op, "=", 0});
      ++i;
      continue;
    }
    if (c == '!') {
      if (i + 1 < n && sql[i + 1] == '=') {
        out.push_back({TokenType::Op, "!=", 0});
        i += 2;
        continue;
      }
      fail("expected '=' after '!'");
    }
    if (c == '<') {
      if (i + 1 < n && sql[i + 1] == '=') { out.push_back({TokenType::Op, "<=", 0}); i += 2; continue; }
      if (i + 1 < n && sql[i + 1] == '>') { out.push_back({TokenType::Op, "<>", 0}); i += 2; continue; }
      out.push_back({TokenType::Op, "<", 0});
      ++i;
      continue;
    }
    if (c == '>') {
      if (i + 1 < n && sql[i + 1] == '=') { out.push_back({TokenType::Op, ">=", 0}); i += 2; continue; }
      out.push_back({TokenType::Op, ">", 0});
      ++i;
      continue;
    }

    // String literal: '...'  ('' is an escaped single quote).
    if (c == '\'') {
      ++i;  // skip opening quote
      std::string s;
      bool closed = false;
      while (i < n) {
        if (sql[i] == '\'') {
          if (i + 1 < n && sql[i + 1] == '\'') { s.push_back('\''); i += 2; continue; }
          closed = true;
          ++i;
          break;
        }
        s.push_back(sql[i]);
        ++i;
      }
      if (!closed) fail("unterminated string literal");
      out.push_back({TokenType::StrLit, std::move(s), 0});
      continue;
    }

    // Integer literal (optionally signed). A leading '-' is only a sign when
    // followed by a digit; bare '-' is not an operator MiniDB supports.
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
      size_t start = i;
      if (c == '-') ++i;
      while (i < n && std::isdigit(static_cast<unsigned char>(sql[i]))) ++i;
      std::string num = sql.substr(start, i - start);
      out.push_back({TokenType::IntLit, num, std::stoll(num)});
      continue;
    }

    // Identifier or keyword.
    if (isIdentStart(c)) {
      size_t start = i;
      while (i < n && isIdentChar(sql[i])) ++i;
      std::string word = sql.substr(start, i - start);
      std::string up = upper(word);
      if (keywords().count(up)) {
        out.push_back({TokenType::Keyword, up, 0});
      } else {
        out.push_back({TokenType::Ident, word, 0});
      }
      continue;
    }

    fail(std::string("unexpected character '") + c + "'");
  }

  out.push_back({TokenType::End, "", 0});
  return out;
}

// ---- Parser (recursive descent over the token stream) ----

namespace {

class ParserImpl {
 public:
  explicit ParserImpl(std::vector<Token> toks) : toks_(std::move(toks)) {}

  Statement parse() {
    Statement stmt;
    if (acceptKeyword("EXPLAIN")) stmt.explain = true;

    const Token& t = peek();
    if (t.type != TokenType::Keyword) fail("expected a statement keyword");
    if (t.text == "SELECT") {
      stmt.node = parseSelect();
    } else if (t.text == "INSERT") {
      stmt.node = parseInsert();
    } else if (t.text == "DELETE") {
      stmt.node = parseDelete();
    } else {
      fail("unsupported statement '" + t.text + "'");
    }

    if (peek().type != TokenType::End)
      fail("unexpected trailing input near '" + peek().text + "'");
    return stmt;
  }

 private:
  // -- token cursor helpers --
  const Token& peek() const { return toks_[pos_]; }
  const Token& advance() { return toks_[pos_++]; }

  bool acceptKeyword(const std::string& kw) {
    if (peek().type == TokenType::Keyword && peek().text == kw) {
      ++pos_;
      return true;
    }
    return false;
  }

  void expectKeyword(const std::string& kw) {
    if (!acceptKeyword(kw)) fail("expected keyword '" + kw + "', got '" + peek().text + "'");
  }

  void expect(TokenType type, const char* what) {
    if (peek().type != type) fail(std::string("expected ") + what + ", got '" + peek().text + "'");
    ++pos_;
  }

  std::string expectIdent(const char* what) {
    if (peek().type != TokenType::Ident) fail(std::string("expected ") + what + ", got '" + peek().text + "'");
    return advance().text;
  }

  // <col> ::= ident ['.' ident]   (qualified column reference)
  ColumnRef parseColumnRef() {
    ColumnRef ref;
    std::string first = expectIdent("a column name");
    if (peek().type == TokenType::Dot) {
      advance();  // '.'
      ref.table = first;
      ref.column = expectIdent("a column name after '.'");
    } else {
      ref.column = first;
    }
    return ref;
  }

  // <literal> ::= IntLit | StrLit
  LiteralVal parseLiteral() {
    const Token& t = peek();
    if (t.type == TokenType::IntLit) {
      advance();
      return LiteralVal{ValueType::Int, t.int_val, ""};
    }
    if (t.type == TokenType::StrLit) {
      advance();
      return LiteralVal{ValueType::Text, 0, t.text};
    }
    fail("expected a literal value, got '" + t.text + "'");
  }

  CompareOp parseCompareOp() {
    if (peek().type != TokenType::Op) fail("expected a comparison operator, got '" + peek().text + "'");
    const std::string op = advance().text;
    if (op == "=") return CompareOp::Eq;
    if (op == "!=" || op == "<>") return CompareOp::Ne;
    if (op == "<") return CompareOp::Lt;
    if (op == "<=") return CompareOp::Le;
    if (op == ">") return CompareOp::Gt;
    if (op == ">=") return CompareOp::Ge;
    fail("unknown comparison operator '" + op + "'");
  }

  // <col> <op> <literal>
  Condition parseCondition() {
    Condition cond;
    cond.col = parseColumnRef();
    cond.op = parseCompareOp();
    cond.val = parseLiteral();
    return cond;
  }

  // Boolean WHERE expression with precedence: OR binds looser than AND, and
  // parentheses override both. Built left-associatively into a WhereExpr tree.
  std::shared_ptr<WhereExpr> makeLeaf(Condition c) {
    auto e = std::make_shared<WhereExpr>();
    e->kind = WhereExpr::Kind::Leaf;
    e->leaf = std::move(c);
    return e;
  }

  std::shared_ptr<WhereExpr> parseCmp() {
    if (peek().type == TokenType::LParen) {
      advance();
      auto inner = parseOrExpr();
      expect(TokenType::RParen, "')' to close a parenthesised condition");
      return inner;
    }
    return makeLeaf(parseCondition());
  }

  std::shared_ptr<WhereExpr> parseAndExpr() {
    auto left = parseCmp();
    while (acceptKeyword("AND")) {
      auto node = std::make_shared<WhereExpr>();
      node->kind = WhereExpr::Kind::And;
      node->left = left;
      node->right = parseCmp();
      left = node;
    }
    return left;
  }

  std::shared_ptr<WhereExpr> parseOrExpr() {
    auto left = parseAndExpr();
    while (acceptKeyword("OR")) {
      auto node = std::make_shared<WhereExpr>();
      node->kind = WhereExpr::Kind::Or;
      node->left = left;
      node->right = parseAndExpr();
      left = node;
    }
    return left;
  }

  SelectStatement parseSelect() {
    expectKeyword("SELECT");
    SelectStatement sel;

    // Select list: '*' or comma-separated column refs.
    if (peek().type == TokenType::Star) {
      advance();
      sel.star = true;
    } else {
      sel.columns.push_back(parseColumnRef());
      while (peek().type == TokenType::Comma) {
        advance();
        sel.columns.push_back(parseColumnRef());
      }
    }

    expectKeyword("FROM");
    sel.from = expectIdent("a table name after FROM");

    // Optional single JOIN ... ON a = b.
    if (acceptKeyword("JOIN")) {
      JoinClause jc;
      jc.table = expectIdent("a table name after JOIN");
      expectKeyword("ON");
      jc.left = parseColumnRef();
      if (parseCompareOp() != CompareOp::Eq) fail("JOIN supports equi-joins only (use '=')");
      jc.right = parseColumnRef();
      sel.join = std::move(jc);
    }

    if (acceptKeyword("WHERE")) sel.where = parseOrExpr();
    return sel;
  }

  InsertStatement parseInsert() {
    expectKeyword("INSERT");
    expectKeyword("INTO");
    InsertStatement ins;
    ins.table = expectIdent("a table name after INTO");
    expectKeyword("VALUES");
    expect(TokenType::LParen, "'(' before the value list");
    ins.values.push_back(parseLiteral());
    while (peek().type == TokenType::Comma) {
      advance();
      ins.values.push_back(parseLiteral());
    }
    expect(TokenType::RParen, "')' after the value list");
    return ins;
  }

  DeleteStatement parseDelete() {
    expectKeyword("DELETE");
    expectKeyword("FROM");
    DeleteStatement del;
    del.table = expectIdent("a table name after FROM");
    if (acceptKeyword("WHERE")) del.where = parseOrExpr();
    return del;
  }

  std::vector<Token> toks_;
  size_t pos_ = 0;
};

}  // namespace

Statement Parser::parse(const std::string& sql) {
  ParserImpl impl(tokenize(sql));
  return impl.parse();
}

}  // namespace minidb
