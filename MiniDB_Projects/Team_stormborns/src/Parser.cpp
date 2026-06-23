#include "Parser.h"

#include <stdexcept>

// ── Constructor ─────────────────────────────────────────────────────────

Parser::Parser(const std::vector<Token> &tokens) : tokens_(tokens), pos_(0) {}

// ── Token navigation helpers ────────────────────────────────────────────

const Token &Parser::current() const { return tokens_[pos_]; }

const Token &Parser::peek() const {
  if (pos_ + 1 < static_cast<int>(tokens_.size())) {
    return tokens_[pos_ + 1];
  }
  return tokens_.back(); // EOF
}

void Parser::advance() {
  if (pos_ < static_cast<int>(tokens_.size()) - 1) {
    pos_++;
  }
}

void Parser::expect(TokenType type, const std::string &context) {
  if (current().type != type) {
    throw std::runtime_error("Parse error: expected " + context + " but got '" +
                             current().value + "' at position " +
                             std::to_string(current().position));
  }
  advance();
}

bool Parser::match(TokenType type) const { return current().type == type; }

// ── Main parse entry point ──────────────────────────────────────────────

std::unique_ptr<ASTNode> Parser::parse() {
  /*
   * The parser dispatches based on the first token:
   *   SELECT → parseSelect()
   *   INSERT → parseInsert()
   *   DELETE → parseDelete()
   *   SHOW   → parseShow()
   *
   * This is the "predict" step of LL(1) parsing — we look at one
   * token ahead to decide which production rule to apply.
   */
  if (match(TokenType::SELECT)) {
    return parseSelect();
  } else if (match(TokenType::INSERT)) {
    return parseInsert();
  } else if (match(TokenType::DELETE)) {
    return parseDelete();
  } else if (match(TokenType::SHOW)) {
    return parseShow();
  } else {
    throw std::runtime_error(
        "Parse error: expected SELECT, INSERT, DELETE, or SHOW but got '" +
        current().value + "'");
  }
}

// ── SELECT parsing ──────────────────────────────────────────────────────

std::unique_ptr<ASTNode> Parser::parseSelect() {
  /*
   * Grammar:
   *   SELECT * FROM tableName [JOIN table2 ON t1.c1 = t2.c2] [WHERE col op val]
   *
   * We parse this strictly left-to-right, consuming tokens as we go.
   */
  auto node = std::make_unique<SelectNode>();

  // SELECT
  expect(TokenType::SELECT, "SELECT");

  // * (we only support SELECT *)
  expect(TokenType::STAR, "*");

  // FROM tableName
  expect(TokenType::FROM, "FROM");
  node->tableName = current().value;
  expect(TokenType::IDENTIFIER, "table name");

  // Optional: JOIN table2 ON t1.col1 = t2.col2
  if (match(TokenType::JOIN)) {
    node->hasJoin = true;
    advance(); // consume JOIN

    node->joinTable = current().value;
    expect(TokenType::IDENTIFIER, "join table name");

    expect(TokenType::ON, "ON");

    // Parse t1.col1
    node->joinTable1 = current().value;
    expect(TokenType::IDENTIFIER, "join table qualifier");
    expect(TokenType::DOT, ".");
    node->joinCol1 = current().value;
    expect(TokenType::IDENTIFIER, "join column name");

    // =
    expect(TokenType::EQUALS, "=");

    // Parse t2.col2
    node->joinTable2 = current().value;
    expect(TokenType::IDENTIFIER, "join table qualifier");
    expect(TokenType::DOT, ".");
    node->joinCol2 = current().value;
    expect(TokenType::IDENTIFIER, "join column name");
  }

  // Optional: WHERE col op val
  if (match(TokenType::WHERE)) {
    node->hasWhere = true;
    advance(); // consume WHERE
    parseWhereClause(node->whereCol, node->whereOp, node->whereVal);
  }

  // Optional semicolon
  if (match(TokenType::SEMICOLON))
    advance();

  return node;
}

// ── INSERT parsing ──────────────────────────────────────────────────────

std::unique_ptr<ASTNode> Parser::parseInsert() {
  /*
   * Grammar:
   *   INSERT INTO tableName VALUES ( int [, int]* )
   */
  auto node = std::make_unique<InsertNode>();

  // INSERT INTO
  expect(TokenType::INSERT, "INSERT");
  expect(TokenType::INTO, "INTO");

  // tableName
  node->tableName = current().value;
  expect(TokenType::IDENTIFIER, "table name");

  // VALUES
  expect(TokenType::VALUES, "VALUES");

  // ( valueList )
  expect(TokenType::LPAREN, "(");

  // Parse comma-separated integer values
  node->values.push_back(std::stoi(current().value));
  expect(TokenType::INT_LITERAL, "integer value");

  while (match(TokenType::COMMA)) {
    advance(); // consume comma
    node->values.push_back(std::stoi(current().value));
    expect(TokenType::INT_LITERAL, "integer value");
  }

  expect(TokenType::RPAREN, ")");

  // Optional semicolon
  if (match(TokenType::SEMICOLON))
    advance();

  return node;
}

// ── DELETE parsing ──────────────────────────────────────────────────────

std::unique_ptr<ASTNode> Parser::parseDelete() {
  /*
   * Grammar:
   *   DELETE FROM tableName [WHERE col op val]
   */
  auto node = std::make_unique<DeleteNode>();

  // DELETE FROM
  expect(TokenType::DELETE, "DELETE");
  expect(TokenType::FROM, "FROM");

  // tableName
  node->tableName = current().value;
  expect(TokenType::IDENTIFIER, "table name");

  // Optional WHERE
  if (match(TokenType::WHERE)) {
    node->hasWhere = true;
    advance();
    parseWhereClause(node->whereCol, node->whereOp, node->whereVal);
  }

  // Optional semicolon
  if (match(TokenType::SEMICOLON))
    advance();

  return node;
}

// ── WHERE clause helper ─────────────────────────────────────────────────

void Parser::parseWhereClause(std::string &col, CompOp &op, int32_t &val) {
  /*
   * WHERE clause grammar:
   *   column_name (= | < | >) integer_literal
   *
   * We extract the column name, comparison operator, and value.
   */
  col = current().value;
  expect(TokenType::IDENTIFIER, "column name");

  if (match(TokenType::EQUALS)) {
    op = CompOp::EQUALS;
  } else if (match(TokenType::LESS)) {
    op = CompOp::LESS;
  } else if (match(TokenType::GREATER)) {
    op = CompOp::GREATER;
  } else {
    throw std::runtime_error(
        "Parse error: expected comparison operator (=, <, >) but got '" +
        current().value + "'");
  }
  advance(); // consume operator

  val = std::stoi(current().value);
  expect(TokenType::INT_LITERAL, "integer literal");
}

std::unique_ptr<ASTNode> Parser::parseShow() {
  expect(TokenType::SHOW, "SHOW");
  if (match(TokenType::TABLES)) {
    advance(); // consume TABLES
    if (match(TokenType::SEMICOLON)) {
      advance();
    }
    return std::make_unique<ShowTablesAST>();
  } else if (match(TokenType::SCHEMA)) {
    advance(); // consume SCHEMA
    std::string tableName = current().value;
    expect(TokenType::IDENTIFIER, "table name");
    if (match(TokenType::SEMICOLON)) {
      advance();
    }
    return std::make_unique<ShowSchemaAST>(tableName);
  } else {
    throw std::runtime_error("Parse error: expected TABLES or SCHEMA after SHOW, got '" + current().value + "'");
  }
}
