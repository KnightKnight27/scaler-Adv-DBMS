#pragma once

#include <string>
#include <vector>

namespace axiomdb {

// ===========================================================================
// Lexer (tokenizer) for AxiomDB's SQL subset.
//
// The lexer turns a raw SQL string into a flat vector of tokens that the
// recursive-descent parser consumes one at a time.  We tokenize the WHOLE
// input up front (rather than streaming) because the parser is small and
// occasionally benefits from one-token lookahead; a fully materialized vector
// makes that lookahead trivial and keeps positions stable for error messages.
//
// Design notes worth defending in a viva:
//   * Keywords are NOT a distinct token class.  We emit every word as an
//     `Identifier` and let the parser decide, via case-insensitive compares,
//     whether a given identifier is acting as a keyword in context.  SQL has no
//     reserved-word problem for our tiny grammar, and this keeps the lexer
//     dumb and the keyword list owned entirely by the parser.
//   * Each token records its byte offset in the source so the parser can
//     produce "near column N" diagnostics.
//   * The lexer never throws.  A bad character / unterminated string produces
//     a token of kind `Error` carrying a message; the parser surfaces it.
// ===========================================================================

enum class TokenKind {
  Identifier,  // keyword-or-name: [A-Za-z_][A-Za-z0-9_]*
  Integer,     // run of digits, no '.' or exponent
  Double,      // numeric literal containing '.' or an exponent
  String,      // single-quoted string literal (already un-escaped)
  Punct,       // an operator / punctuation token (text holds the symbol)
  Error,       // lexing failed (text holds the human-readable reason)
  End,         // sentinel: end of input
};

struct Token {
  TokenKind kind;
  std::string text;  // the literal/identifier/operator/error text
  size_t pos;        // byte offset of the token's first character in the source

  Token(TokenKind k, std::string t, size_t p)
      : kind(k), text(std::move(t)), pos(p) {}
};

// Tokenize `sql` into a token vector that always ends with a single End token.
// If a lexing error occurs, an Error token is appended (followed by End) and
// tokenization stops; the parser checks for Error and reports it gracefully.
class Lexer {
 public:
  explicit Lexer(const std::string& sql) : src_(sql) {}

  // Run the scan and return the full token list (terminated by End).
  std::vector<Token> tokenize();

 private:
  // Character-level helpers operating on the current cursor `i_`.
  bool at_end() const { return i_ >= src_.size(); }
  char peek(size_t ahead = 0) const {
    size_t j = i_ + ahead;
    return j < src_.size() ? src_[j] : '\0';
  }

  void skip_whitespace_and_comments();
  Token lex_number();      // Integer or Double
  Token lex_string();      // '...'
  Token lex_identifier();  // [A-Za-z_][A-Za-z0-9_]*
  Token lex_punct();       // operators / punctuation

  const std::string& src_;
  size_t i_ = 0;  // current scan cursor (byte offset into src_)
};

}  // namespace axiomdb
