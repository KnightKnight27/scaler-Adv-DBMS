#ifndef MINIDB_LEXER_H
#define MINIDB_LEXER_H

#include <string>
#include <vector>

/**
 * Token types recognized by the MiniDB SQL Lexer.
 *
 * DESIGN NOTE: We support a minimal but complete SQL subset:
 *   SELECT, INSERT, DELETE, FROM, WHERE, JOIN, ON, INTO, VALUES,
 *   AND, comparison operators, identifiers, integer literals.
 *
 * This is deliberately limited — a production lexer would need to
 * handle string literals, floating point, aliases (AS), ORDER BY,
 * GROUP BY, etc. Our goal is to demonstrate the lexer → parser →
 * AST pipeline clearly for the viva.
 */
enum class TokenType {
  // SQL Keywords
  SELECT,
  INSERT,
  DELETE,
  FROM,
  WHERE,
  JOIN,
  ON,
  INTO,
  VALUES,
  AND,
  SET,
  CREATE,
  TABLE,
  SHOW,
  TABLES,
  INT_TYPE,
  VARCHAR_TYPE,
  BEGIN_TXN,
  COMMIT_TXN,
  ROLLBACK_TXN,

  // Operators & Punctuation
  STAR,      // *
  COMMA,     // ,
  LPAREN,    // (
  RPAREN,    // )
  SEMICOLON, // ;
  DOT,       // .
  EQUALS,    // =
  LESS,      // <
  GREATER,   // >

  // Literals & Identifiers
  IDENTIFIER,  // table names, column names
  INT_LITERAL, // integer constants
  STRING_LITERAL, // single-quoted strings

  // End of input
  EOF_TOKEN
};

/**
 * A single token produced by the Lexer.
 */
struct Token {
  TokenType type;
  std::string value; // raw text of the token
  int position;      // character position in the input (for error messages)

  Token() : type(TokenType::EOF_TOKEN), position(0) {}
  Token(TokenType t, const std::string &v, int pos)
      : type(t), value(v), position(pos) {}
};

/**
 * Lexer (Scanner) for MiniDB SQL.
 *
 * Converts a raw SQL string into a stream of tokens. This is the first
 * stage of the query processing pipeline:
 *
 *   SQL String → [Lexer] → Token Stream → [Parser] → AST
 *
 * IMPLEMENTATION:
 * The lexer uses a simple character-by-character scan with:
 *   1. Whitespace skipping
 *   2. Single-character operator recognition
 *   3. Multi-character keyword/identifier recognition (case-insensitive)
 *   4. Integer literal recognition
 *
 * CASE INSENSITIVITY:
 * SQL keywords are case-insensitive (SELECT = select = Select).
 * We convert keywords to uppercase during tokenization but preserve
 * the original case for identifiers (table/column names).
 */
class Lexer {
public:
  explicit Lexer(const std::string &input);

  /** Tokenize the entire input string. */
  std::vector<Token> tokenize();

private:
  char currentChar() const;
  char peek() const;
  void advance();
  void skipWhitespace();
  Token readNumber();
  Token readStringLiteral();
  Token readIdentifierOrKeyword();

  std::string input_;
  int pos_;
};

#endif // MINIDB_LEXER_H
