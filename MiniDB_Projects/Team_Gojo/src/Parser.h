#ifndef MINIDB_PARSER_H
#define MINIDB_PARSER_H

#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "Lexer.h"
#include "Record.h"
#include "Schema.h"

// ═══════════════════════════════════════════════════════════════════════
// AST (Abstract Syntax Tree) Node Hierarchy
// ═══════════════════════════════════════════════════════════════════════
//
// The AST is the intermediate representation between the Parser and the
// Optimizer. It captures the LOGICAL intent of the SQL query without
// specifying HOW to execute it (that's the optimizer's job).
//
// We use smart pointers (std::unique_ptr) for AST node ownership because:
//   1. AST nodes are created during parsing and consumed by the optimizer
//   2. The tree structure has clear parent-child ownership
//   3. Smart pointers prevent memory leaks if parsing throws an exception
//
// HIERARCHY:
//   ASTNode (abstract base)
//   ├── SelectNode   — SELECT * FROM t WHERE ... / JOIN ...
//   ├── InsertNode   — INSERT INTO t VALUES (...)
//   └── DeleteNode   — DELETE FROM t WHERE ...
// ═══════════════════════════════════════════════════════════════════════

/** Comparison operators used in WHERE clauses. */
enum class CompOp { EQUALS, LESS, GREATER, NONE };

/** Base class for all AST nodes. */
struct ASTNode {
  virtual ~ASTNode() = default;
  virtual std::string toString() const = 0;
};

/**
 * AST node for SELECT queries.
 *
 * Supports:
 *   SELECT * FROM table
 *   SELECT * FROM table WHERE col = val
 *   SELECT * FROM t1 JOIN t2 ON t1.col = t2.col
 *   SELECT * FROM t1 JOIN t2 ON t1.col = t2.col WHERE col = val
 */
struct SelectNode : public ASTNode {
  std::string tableName; // FROM table

  // JOIN clause (optional)
  bool hasJoin = false;
  std::string joinTable;  // JOIN table
  std::string joinCol1;   // ON t1.col1 = ...
  std::string joinCol2;   //    ... = t2.col2
  std::string joinTable1; // Table qualifier for col1
  std::string joinTable2; // Table qualifier for col2

  // WHERE clause (optional)
  bool hasWhere = false;
  std::string whereCol; // WHERE col ...
  CompOp whereOp = CompOp::NONE;
  int32_t whereVal = 0; // ... = val

  std::string toString() const override {
    std::string s = "SELECT * FROM " + tableName;
    if (hasJoin) {
      s += " JOIN " + joinTable + " ON " + joinTable1 + "." + joinCol1 + " = " +
           joinTable2 + "." + joinCol2;
    }
    if (hasWhere) {
      std::string op = (whereOp == CompOp::EQUALS) ? "="
                       : (whereOp == CompOp::LESS) ? "<"
                                                   : ">";
      s += " WHERE " + whereCol + " " + op + " " + std::to_string(whereVal);
    }
    return s;
  }
};

/**
 * AST node for INSERT queries.
 *
 * Supports: INSERT INTO table VALUES (id, val)
 */
struct InsertNode : public ASTNode {
  std::string tableName;
  std::vector<Value> values;

  std::string toString() const override {
    std::string s = "INSERT INTO " + tableName + " VALUES (";
    for (size_t i = 0; i < values.size(); i++) {
      if (i > 0)
        s += ", ";
      std::visit([&s](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int>) {
          s += std::to_string(arg);
        } else {
          s += "'" + arg + "'";
        }
      }, values[i]);
    }
    s += ")";
    return s;
  }
};

using InsertAST = InsertNode;

/**
 * AST node for CREATE TABLE queries.
 *
 * Supports: CREATE TABLE table_name (col1 INT, col2 VARCHAR)
 */
struct CreateAST : public ASTNode {
  std::string tableName;
  std::vector<ColumnDef> columns;

  std::string toString() const override {
    std::string s = "CREATE TABLE " + tableName + " (";
    for (size_t i = 0; i < columns.size(); i++) {
      if (i > 0) s += ", ";
      s += columns[i].name + " " + dataTypeToString(columns[i].type);
    }
    s += ")";
    return s;
  }
};

using CreateNode = CreateAST;

/** AST node for SHOW TABLES. */
struct ShowTablesAST : public ASTNode {
  std::string toString() const override { return "SHOW TABLES"; }
};

/**
 * AST node for DELETE queries.
 *
 * Supports: DELETE FROM table WHERE col = val
 */
struct DeleteNode : public ASTNode {
  std::string tableName;
  bool hasWhere = false;
  std::string whereCol;
  CompOp whereOp = CompOp::NONE;
  int32_t whereVal = 0;

  std::string toString() const override {
    std::string s = "DELETE FROM " + tableName;
    if (hasWhere) {
      std::string op = (whereOp == CompOp::EQUALS) ? "="
                       : (whereOp == CompOp::LESS) ? "<"
                                                   : ">";
      s += " WHERE " + whereCol + " " + op + " " + std::to_string(whereVal);
    }
    return s;
  }
};

// ═══════════════════════════════════════════════════════════════════════
// Recursive-Descent Parser
// ═══════════════════════════════════════════════════════════════════════
//
// PARSING STRATEGY:
// We use a recursive-descent parser, which is the simplest and most
// readable approach for our SQL subset. Each grammar rule maps to a
// method that consumes tokens and builds the corresponding AST node.
//
// GRAMMAR (informal):
//   Statement   → SelectStmt | InsertStmt | DeleteStmt
//   SelectStmt  → SELECT STAR FROM Identifier [JoinClause] [WhereClause]
//   InsertStmt  → INSERT INTO Identifier VALUES LPAREN ValueList RPAREN
//   DeleteStmt  → DELETE FROM Identifier [WhereClause]
//   JoinClause  → JOIN Identifier ON QualCol EQUALS QualCol
//   WhereClause → WHERE Identifier CompOp IntLiteral
//   QualCol     → Identifier DOT Identifier
//   CompOp      → EQUALS | LESS | GREATER
//
// WHY RECURSIVE DESCENT?
// For our small grammar, recursive descent is ideal because:
//   1. Each grammar rule becomes a clearly-named function
//   2. Error messages can pinpoint exactly which rule failed
//   3. No external parser generator dependency (like yacc/bison)
//   4. Easy to extend with new SQL features
//
// A production SQL parser would use a more sophisticated technique
// (e.g., Pratt parsing for expressions, or a parser generator) to
// handle operator precedence, subqueries, and complex expressions.
// ═══════════════════════════════════════════════════════════════════════

class Parser {
public:
  explicit Parser(const std::vector<Token> &tokens);

  /** Parse the token stream into an AST. */
  std::unique_ptr<ASTNode> parse();

private:
  // ── Token navigation ────────────────────────────────────────────
  const Token &current() const;
  const Token &peek() const;
  void advance();
  void expect(TokenType type, const std::string &context);
  bool match(TokenType type) const;

  // ── Grammar rules ───────────────────────────────────────────────
  std::unique_ptr<ASTNode> parseSelect();
  std::unique_ptr<ASTNode> parseInsert();
  std::unique_ptr<ASTNode> parseCreate();
  std::unique_ptr<ASTNode> parseShowTables();
  std::unique_ptr<ASTNode> parseDelete();
  Value parseLiteralValue();
  DataType parseDataType();

  // ── WHERE clause helper ─────────────────────────────────────────
  void parseWhereClause(std::string &col, CompOp &op, int32_t &val);

  std::vector<Token> tokens_;
  int pos_;
};

#endif // MINIDB_PARSER_H
