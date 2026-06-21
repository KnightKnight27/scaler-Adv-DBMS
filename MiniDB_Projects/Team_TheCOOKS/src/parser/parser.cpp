#include "parser/parser.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "catalog/schema.h"
#include "catalog/value.h"
#include "parser/ast.h"
#include "parser/lexer.h"

namespace walterdb {

namespace {

// ===========================================================================
// Recursive-descent parser.
//
// The parser walks the token vector produced by the Lexer and builds the AST
// from parser/ast.h.  Two structural choices to defend in a viva:
//
//   1. Errors are reported via a private ParseError exception that we catch at
//      the single public boundary (parse_sql).  Internally, throwing keeps the
//      descent code linear and readable -- every helper can assume success and
//      simply `expect(...)`.  The exception NEVER escapes parse_sql; the public
//      contract is a ParseResult with a message, never a thrown exception.
//
//   2. Expressions use precedence climbing (a.k.a. the "binary expression with
//      a minimum-precedence parameter" technique).  Instead of one function per
//      precedence level, a single parse_binary(min_prec) loop consults a table
//      of operator precedences.  This is compact and makes the precedence order
//      (OR < AND < comparison < +,- < *,/) a single source of truth.
// ===========================================================================

struct ParseError {
  std::string message;
};

[[noreturn]] void fail(const std::string& msg) { throw ParseError{msg}; }

// Case-insensitive equality, used for keyword matching (keywords are
// case-insensitive but identifiers we store verbatim).
bool iequals(const std::string& a, const char* b) {
  size_t n = a.size();
  for (size_t k = 0; k < n; ++k) {
    if (std::tolower(static_cast<unsigned char>(a[k])) !=
        std::tolower(static_cast<unsigned char>(b[k]))) {
      return false;
    }
    if (b[k] == '\0') return false;  // b shorter than a
  }
  return b[n] == '\0';  // both ended together
}

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {}

  StmtPtr parse_statement() {
    // Surface any lexer error before we try to parse structure.
    for (const Token& t : toks_) {
      if (t.kind == TokenKind::Error) {
        fail(t.text + " (at position " + std::to_string(t.pos) + ")");
      }
    }

    StmtPtr stmt = parse_one();

    // Optional trailing ';' then nothing else may remain.
    accept_punct(";");
    if (!at_end()) {
      fail("unexpected token '" + cur().text + "' after end of statement");
    }
    return stmt;
  }

 private:
  // ---- token cursor ------------------------------------------------------

  const Token& cur() const { return toks_[pos_]; }
  // Safe lookahead: returns the token `ahead` positions past the cursor, or the
  // terminating End token if that would run off the end.  (The vector always
  // ends with exactly one End token, so we clamp to it.)
  const Token& peek_tok(size_t ahead) const {
    size_t j = pos_ + ahead;
    return j < toks_.size() ? toks_[j] : toks_.back();
  }
  bool at_end() const { return cur().kind == TokenKind::End; }
  void advance() {
    if (!at_end()) ++pos_;
  }

  // True if the current token is the (case-insensitive) keyword `kw`.
  bool is_keyword(const char* kw) const {
    return cur().kind == TokenKind::Identifier && iequals(cur().text, kw);
  }
  // True if the current token is the punctuation `p`.
  bool is_punct(const char* p) const {
    return cur().kind == TokenKind::Punct && cur().text == p;
  }

  // Consume the current token iff it is keyword `kw`; report whether it was.
  bool accept_keyword(const char* kw) {
    if (is_keyword(kw)) {
      advance();
      return true;
    }
    return false;
  }
  bool accept_punct(const char* p) {
    if (is_punct(p)) {
      advance();
      return true;
    }
    return false;
  }
  void expect_keyword(const char* kw) {
    if (!accept_keyword(kw)) {
      fail(std::string("expected '") + kw + "' but found " + describe_cur());
    }
  }
  void expect_punct(const char* p) {
    if (!accept_punct(p)) {
      fail(std::string("expected '") + p + "' but found " + describe_cur());
    }
  }

  // Consume and return an identifier that is NOT acting as punctuation; used
  // for names (table, column, alias).  Errors if the current token is not a
  // bare identifier.
  std::string expect_identifier(const char* what) {
    if (cur().kind != TokenKind::Identifier) {
      fail(std::string("expected ") + what + " but found " + describe_cur());
    }
    std::string name = cur().text;
    advance();
    return name;
  }

  std::string describe_cur() const {
    switch (cur().kind) {
      case TokenKind::End: return "end of input";
      case TokenKind::String: return "string literal '" + cur().text + "'";
      case TokenKind::Integer:
      case TokenKind::Double: return "number '" + cur().text + "'";
      default: return "'" + cur().text + "'";
    }
  }

  // ---- statement dispatch -----------------------------------------------

  StmtPtr parse_one() {
    if (is_keyword("CREATE")) return parse_create_table();
    if (is_keyword("INSERT")) return parse_insert();
    if (is_keyword("SELECT")) return parse_select();
    if (is_keyword("DELETE")) return parse_delete();
    if (is_keyword("EXPLAIN")) return parse_explain();
    if (is_keyword("BEGIN") || is_keyword("COMMIT") ||
        is_keyword("ROLLBACK") || is_keyword("ABORT")) {
      return parse_txn();
    }
    if (at_end()) fail("empty statement");
    fail("unexpected token " + describe_cur() + " at start of statement");
  }

  // ---- CREATE TABLE ------------------------------------------------------

  StmtPtr parse_create_table() {
    expect_keyword("CREATE");
    expect_keyword("TABLE");

    auto stmt = std::make_unique<CreateTableStmt>();
    if (accept_keyword("IF")) {
      expect_keyword("NOT");
      expect_keyword("EXISTS");
      stmt->if_not_exists = true;
    }
    stmt->table = expect_identifier("table name");

    expect_punct("(");
    // At least one column is required.
    do {
      Column col;
      col.name = expect_identifier("column name");

      std::string type_kw = expect_identifier("column type");
      TypeId tid;
      if (!parse_type_name(type_kw, &tid)) {
        fail("unknown column type '" + type_kw + "'");
      }
      col.type = tid;

      // Optional PRIMARY KEY constraint on the column.
      if (accept_keyword("PRIMARY")) {
        expect_keyword("KEY");
        col.primary_key = true;
      }
      stmt->columns.push_back(std::move(col));
    } while (accept_punct(","));
    expect_punct(")");

    if (stmt->columns.empty()) fail("CREATE TABLE requires at least one column");
    return stmt;
  }

  // ---- INSERT ------------------------------------------------------------

  StmtPtr parse_insert() {
    expect_keyword("INSERT");
    expect_keyword("INTO");

    auto stmt = std::make_unique<InsertStmt>();
    stmt->table = expect_identifier("table name");

    // Optional explicit column list.  We disambiguate "(" as a column list vs.
    // the VALUES open-paren by position: a "(" right after the table name is
    // always the column list.
    if (accept_punct("(")) {
      do {
        stmt->columns.push_back(expect_identifier("column name"));
      } while (accept_punct(","));
      expect_punct(")");
    }

    expect_keyword("VALUES");
    // One or more parenthesized value tuples, comma-separated.
    do {
      expect_punct("(");
      std::vector<ExprPtr> row;
      do {
        row.push_back(parse_expr());
      } while (accept_punct(","));
      expect_punct(")");
      stmt->rows.push_back(std::move(row));
    } while (accept_punct(","));

    if (stmt->rows.empty()) fail("INSERT requires at least one VALUES tuple");
    return stmt;
  }

  // ---- SELECT ------------------------------------------------------------

  StmtPtr parse_select() {
    expect_keyword("SELECT");
    auto stmt = std::make_unique<SelectStmt>();

    // Select list: comma-separated items, each "*", "tbl.*", or "expr [AS a]".
    do {
      stmt->items.push_back(parse_select_item());
    } while (accept_punct(","));

    expect_keyword("FROM");
    stmt->from = parse_table_ref();

    // Zero or more [INNER] JOIN ... ON ... clauses.
    while (is_keyword("INNER") || is_keyword("JOIN")) {
      accept_keyword("INNER");  // INNER is optional sugar
      expect_keyword("JOIN");
      JoinClause j;
      j.right = parse_table_ref();
      expect_keyword("ON");
      j.on = parse_expr();
      stmt->joins.push_back(std::move(j));
    }

    if (accept_keyword("WHERE")) {
      stmt->where = parse_expr();
    }
    return stmt;
  }

  SelectItem parse_select_item() {
    SelectItem item;

    // Bare "*" -> all columns.
    if (is_punct("*")) {
      advance();
      item.star = true;
      return item;
    }

    // "tbl.*": an identifier immediately followed by ".*".  We must look ahead
    // two tokens, so only commit to this form when both match.
    if (cur().kind == TokenKind::Identifier &&
        peek_tok(1).kind == TokenKind::Punct && peek_tok(1).text == "." &&
        peek_tok(2).kind == TokenKind::Punct && peek_tok(2).text == "*") {
      item.star = true;
      item.star_table = cur().text;
      advance();  // table
      advance();  // .
      advance();  // *
      return item;
    }

    // Otherwise: an expression, with an optional [AS] alias.
    item.expr = parse_expr();
    item.alias = parse_optional_alias();
    return item;
  }

  // A table reference: name with an optional [AS] alias.
  TableRef parse_table_ref() {
    TableRef ref;
    ref.table = expect_identifier("table name");
    ref.alias = parse_optional_alias();
    return ref;
  }

  // Parse an optional alias: "AS name" or just "name".  Returns "" if none.
  // We must be careful not to swallow a following keyword (FROM, WHERE, JOIN,
  // ON, ...) as an alias, so a bare identifier is only taken as an alias when
  // it is not a structural keyword.
  std::string parse_optional_alias() {
    if (accept_keyword("AS")) {
      return expect_identifier("alias name");
    }
    if (cur().kind == TokenKind::Identifier && !is_structural_keyword()) {
      std::string a = cur().text;
      advance();
      return a;
    }
    return "";
  }

  // Keywords that can legally follow a table-ref / select-expr and therefore
  // must NOT be mistaken for an implicit alias.
  bool is_structural_keyword() const {
    static const char* kws[] = {"FROM", "WHERE", "JOIN",  "INNER", "ON",
                                "AS",   "GROUP", "ORDER", "HAVING", "AND",
                                "OR",   "VALUES"};
    for (const char* kw : kws) {
      if (iequals(cur().text, kw)) return true;
    }
    return false;
  }

  // ---- DELETE ------------------------------------------------------------

  StmtPtr parse_delete() {
    expect_keyword("DELETE");
    expect_keyword("FROM");
    auto stmt = std::make_unique<DeleteStmt>();
    stmt->table = expect_identifier("table name");
    if (accept_keyword("WHERE")) {
      stmt->where = parse_expr();
    }
    return stmt;
  }

  // ---- transactions ------------------------------------------------------

  StmtPtr parse_txn() {
    if (accept_keyword("BEGIN")) {
      accept_keyword("TRANSACTION");  // optional noise word
      return std::make_unique<TxnStmt>(StmtKind::Begin);
    }
    if (accept_keyword("COMMIT")) {
      return std::make_unique<TxnStmt>(StmtKind::Commit);
    }
    // ROLLBACK and ABORT both abort the transaction.
    if (accept_keyword("ROLLBACK") || accept_keyword("ABORT")) {
      return std::make_unique<TxnStmt>(StmtKind::Abort);
    }
    fail("expected a transaction control statement");
  }

  // ---- EXPLAIN -----------------------------------------------------------

  StmtPtr parse_explain() {
    expect_keyword("EXPLAIN");
    auto stmt = std::make_unique<ExplainStmt>();
    // The grammar only explains SELECT, but we accept any inner statement so a
    // clearer error comes from the inner parser if something else appears.
    stmt->inner = parse_one();
    return stmt;
  }

  // ===== expressions: precedence climbing ================================

  // The full precedence ladder, low (binds loosest) to high (binds tightest),
  // matching the assignment grammar:
  //
  //   1  OR
  //   2  AND
  //   3  NOT            (unary prefix -- looser than comparison!)
  //   4  comparison     (= == != <> < <= > >=)
  //   5  additive       (+ -)
  //   6  multiplicative (* /)
  //      unary minus    (handled in parse_unary, just above primary)
  //      primary        (literal | column-ref | '(' expr ')')
  //
  // Note where NOT lands: SQL's NOT is LOOSER than comparison, so "NOT a = b"
  // means "NOT (a = b)".  That is different from C, where '!' is a high-priority
  // prefix.  We therefore treat NOT as a prefix operator sitting at level 3 in
  // the precedence-climbing loop, rather than folding it in with unary minus.
  static constexpr int kPrecNot = 3;

  // Binary operator precedence for the current token, or 0 if it isn't a binary
  // operator here.  (NOT and unary '-' are prefix and handled separately.)
  int binop_precedence(BinOp* out) const {
    if (cur().kind == TokenKind::Identifier) {
      if (iequals(cur().text, "OR")) { *out = BinOp::Or; return 1; }
      if (iequals(cur().text, "AND")) { *out = BinOp::And; return 2; }
      return 0;
    }
    if (cur().kind != TokenKind::Punct) return 0;
    const std::string& s = cur().text;
    if (s == "=" || s == "==") { *out = BinOp::Eq; return 4; }
    if (s == "!=" || s == "<>") { *out = BinOp::Ne; return 4; }
    if (s == "<") { *out = BinOp::Lt; return 4; }
    if (s == "<=") { *out = BinOp::Le; return 4; }
    if (s == ">") { *out = BinOp::Gt; return 4; }
    if (s == ">=") { *out = BinOp::Ge; return 4; }
    if (s == "+") { *out = BinOp::Add; return 5; }
    if (s == "-") { *out = BinOp::Sub; return 5; }
    if (s == "*") { *out = BinOp::Mul; return 6; }
    if (s == "/") { *out = BinOp::Div; return 6; }
    return 0;
  }

  ExprPtr parse_expr() { return parse_binary(1); }

  // Precedence-climbing core.  Parses an operand at the current level, then
  // greedily folds in binary operators whose precedence is >= min_prec.
  //
  // The prefix NOT is woven in here: when the caller's minimum precedence is at
  // or below NOT's level, a leading NOT consumes the rest of the expression at
  // NOT's level (kPrecNot), which is what makes it bind looser than comparison
  // but tighter than AND.  All binary operators are left-associative, so the
  // recursive call for the right operand uses (prec + 1) -- this makes
  // "a - b - c" group as "(a - b) - c".
  ExprPtr parse_binary(int min_prec) {
    ExprPtr left;
    if (min_prec <= kPrecNot && is_keyword("NOT")) {
      advance();
      left = std::make_unique<UnaryExpr>(UnOp::Not, parse_binary(kPrecNot));
    } else {
      left = parse_unary();
    }
    for (;;) {
      BinOp op;
      int prec = binop_precedence(&op);
      if (prec == 0 || prec < min_prec) break;
      advance();  // consume the operator token
      ExprPtr right = parse_binary(prec + 1);
      left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
  }

  // Unary minus: arithmetic negation, the tightest-binding operator just above
  // primary.  Right-recursive so "- - x" nests correctly.  (Logical NOT is
  // handled in parse_binary because it binds far looser than negation.)
  ExprPtr parse_unary() {
    if (is_punct("-")) {
      advance();
      return std::make_unique<UnaryExpr>(UnOp::Neg, parse_unary());
    }
    return parse_primary();
  }

  // Primary: a literal, a (qualified) column reference, or a parenthesized
  // sub-expression.
  ExprPtr parse_primary() {
    const Token& t = cur();
    switch (t.kind) {
      case TokenKind::Integer: {
        // Parse to int64; range errors are reported, not silently wrapped.
        int64_t v;
        try {
          v = std::stoll(t.text);
        } catch (const std::exception&) {
          fail("integer literal out of range: " + t.text);
        }
        advance();
        return std::make_unique<LiteralExpr>(Value::make_integer(v));
      }
      case TokenKind::Double: {
        double v;
        try {
          v = std::stod(t.text);
        } catch (const std::exception&) {
          fail("invalid floating-point literal: " + t.text);
        }
        advance();
        return std::make_unique<LiteralExpr>(Value::make_double(v));
      }
      case TokenKind::String: {
        std::string s = t.text;
        advance();
        return std::make_unique<LiteralExpr>(Value::make_varchar(std::move(s)));
      }
      case TokenKind::Punct:
        if (t.text == "(") {
          advance();
          ExprPtr e = parse_expr();
          expect_punct(")");
          return e;
        }
        fail("unexpected token " + describe_cur() + " in expression");
      case TokenKind::Identifier:
        return parse_identifier_primary();
      default:
        fail("unexpected token " + describe_cur() + " in expression");
    }
  }

  // An identifier in primary position is one of: TRUE / FALSE / NULL keyword
  // literals, or a column reference (possibly qualified "tbl.col").
  ExprPtr parse_identifier_primary() {
    const std::string& word = cur().text;
    if (iequals(word, "TRUE")) {
      advance();
      return std::make_unique<LiteralExpr>(Value::make_boolean(true));
    }
    if (iequals(word, "FALSE")) {
      advance();
      return std::make_unique<LiteralExpr>(Value::make_boolean(false));
    }
    if (iequals(word, "NULL")) {
      advance();
      // The concrete type of NULL is resolved later by the binder; we seed it
      // with Integer as the contract specifies.
      return std::make_unique<LiteralExpr>(Value::make_null(TypeId::Integer));
    }

    // Column reference.  "name" or "tbl . col".
    std::string first = word;
    advance();
    if (accept_punct(".")) {
      std::string col = expect_identifier("column name after '.'");
      return std::make_unique<ColumnRefExpr>(std::move(first), std::move(col));
    }
    return std::make_unique<ColumnRefExpr>("", std::move(first));
  }

  std::vector<Token> toks_;
  size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// expr_to_string: render an Expr back to readable infix text.  Binary ops are
// always fully parenthesized so precedence is unambiguous in the output (the
// tests and EXPLAIN rely on this).
// ---------------------------------------------------------------------------

const char* binop_symbol(BinOp op) {
  switch (op) {
    case BinOp::Eq: return "=";
    case BinOp::Ne: return "!=";
    case BinOp::Lt: return "<";
    case BinOp::Le: return "<=";
    case BinOp::Gt: return ">";
    case BinOp::Ge: return ">=";
    case BinOp::And: return "AND";
    case BinOp::Or: return "OR";
    case BinOp::Add: return "+";
    case BinOp::Sub: return "-";
    case BinOp::Mul: return "*";
    case BinOp::Div: return "/";
  }
  return "?";
}

}  // namespace

std::string expr_to_string(const Expr* e) {
  if (e == nullptr) return "";
  switch (e->kind) {
    case ExprKind::Literal: {
      const auto* lit = static_cast<const LiteralExpr*>(e);
      // Quote string literals so the rendered form is unambiguous.
      if (lit->value.type() == TypeId::Varchar && !lit->value.is_null()) {
        return "'" + lit->value.as_varchar() + "'";
      }
      return lit->value.to_string();
    }
    case ExprKind::ColumnRef: {
      const auto* col = static_cast<const ColumnRefExpr*>(e);
      if (!col->table.empty()) return col->table + "." + col->column;
      return col->column;
    }
    case ExprKind::Binary: {
      const auto* b = static_cast<const BinaryExpr*>(e);
      return "(" + expr_to_string(b->left.get()) + " " + binop_symbol(b->op) +
             " " + expr_to_string(b->right.get()) + ")";
    }
    case ExprKind::Unary: {
      const auto* u = static_cast<const UnaryExpr*>(e);
      if (u->op == UnOp::Not) {
        return "(NOT " + expr_to_string(u->operand.get()) + ")";
      }
      return "(-" + expr_to_string(u->operand.get()) + ")";
    }
  }
  return "";
}

ParseResult parse_sql(const std::string& sql) {
  // The single boundary where parser exceptions are converted into the public
  // ParseResult contract.  Nothing thrown here escapes to the caller.
  try {
    Lexer lexer(sql);
    Parser parser(lexer.tokenize());
    StmtPtr stmt = parser.parse_statement();
    return ParseResult{std::move(stmt), ""};
  } catch (const ParseError& e) {
    return ParseResult{nullptr, "syntax error: " + e.message};
  } catch (const std::exception& e) {
    // Defensive: any unexpected standard exception (e.g. from a library call)
    // is reported rather than allowed to crash the REPL.
    return ParseResult{nullptr, std::string("parse failed: ") + e.what()};
  }
}

}  // namespace walterdb
