#include "parser/lexer.h"

#include <cctype>

namespace walterdb {

namespace {

// We classify identifier characters ourselves rather than leaning on
// std::isalpha so the rules are explicit and locale-independent: an identifier
// starts with a letter or underscore and continues with letters, digits, or
// underscores.  (std::isalpha can vary with locale; SQL identifiers should not.)
bool is_ident_start(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool is_ident_part(char c) {
  return is_ident_start(c) || (c >= '0' && c <= '9');
}
bool is_digit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

void Lexer::skip_whitespace_and_comments() {
  // Loop because comments and whitespace can interleave arbitrarily.
  for (;;) {
    while (!at_end() &&
           (peek() == ' ' || peek() == '\t' || peek() == '\n' ||
            peek() == '\r' || peek() == '\f' || peek() == '\v')) {
      ++i_;
    }
    // SQL line comment: "--" runs to end of line.
    if (peek() == '-' && peek(1) == '-') {
      i_ += 2;
      while (!at_end() && peek() != '\n') ++i_;
      continue;  // re-check for trailing whitespace / another comment
    }
    break;
  }
}

Token Lexer::lex_number() {
  size_t start = i_;
  bool is_double = false;

  while (is_digit(peek())) ++i_;

  // Fractional part: a '.' makes this a double.  We only consume the dot if it
  // is part of the number (the lexer has no other use for '.' adjacent to
  // digits -- table.column qualification uses an identifier on each side).
  if (peek() == '.') {
    is_double = true;
    ++i_;
    while (is_digit(peek())) ++i_;
  }

  // Exponent part: e / E, optional sign, then required digits.  Also a double.
  if (peek() == 'e' || peek() == 'E') {
    size_t save = i_;
    ++i_;
    if (peek() == '+' || peek() == '-') ++i_;
    if (is_digit(peek())) {
      is_double = true;
      while (is_digit(peek())) ++i_;
    } else {
      // "1e" with no exponent digits -- not a valid exponent; rewind so the
      // 'e' is left for the identifier lexer (it won't be, here, but rewinding
      // keeps the number well-formed and avoids swallowing a stray 'e').
      i_ = save;
    }
  }

  return Token(is_double ? TokenKind::Double : TokenKind::Integer,
               src_.substr(start, i_ - start), start);
}

Token Lexer::lex_string() {
  size_t start = i_;
  ++i_;  // consume opening quote
  std::string out;
  for (;;) {
    if (at_end()) {
      return Token(TokenKind::Error, "unterminated string literal", start);
    }
    char c = peek();
    if (c == '\'') {
      // A doubled '' inside a string is an escaped single quote.
      if (peek(1) == '\'') {
        out.push_back('\'');
        i_ += 2;
        continue;
      }
      ++i_;  // consume closing quote
      return Token(TokenKind::String, std::move(out), start);
    }
    out.push_back(c);
    ++i_;
  }
}

Token Lexer::lex_identifier() {
  size_t start = i_;
  while (is_ident_part(peek())) ++i_;
  return Token(TokenKind::Identifier, src_.substr(start, i_ - start), start);
}

Token Lexer::lex_punct() {
  size_t start = i_;
  char c = peek();

  // Two-character operators first (longest match wins): <=, >=, <>, !=, ==.
  char n = peek(1);
  auto two = [&](char a, char b) { return c == a && n == b; };
  if (two('<', '=') || two('>', '=') || two('<', '>') || two('!', '=') ||
      two('=', '=')) {
    i_ += 2;
    return Token(TokenKind::Punct, src_.substr(start, 2), start);
  }

  // Single-character punctuation / operators.
  switch (c) {
    case '(': case ')': case ',': case ';': case '.':
    case '+': case '-': case '*': case '/':
    case '=': case '<': case '>':
      ++i_;
      return Token(TokenKind::Punct, std::string(1, c), start);
    default:
      ++i_;  // advance so a repeated call makes progress even on error
      return Token(TokenKind::Error,
                   std::string("unexpected character '") + c + "'", start);
  }
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  for (;;) {
    skip_whitespace_and_comments();
    if (at_end()) break;

    char c = peek();
    Token tok = [&]() -> Token {
      if (is_ident_start(c)) return lex_identifier();
      if (is_digit(c)) return lex_number();
      // A leading '.' followed by a digit (e.g. ".5") is still a numeric
      // literal; route it to the number lexer.
      if (c == '.' && is_digit(peek(1))) return lex_number();
      if (c == '\'') return lex_string();
      return lex_punct();
    }();

    if (tok.kind == TokenKind::Error) {
      tokens.push_back(std::move(tok));
      break;  // stop on first lexing error; parser reports it
    }
    tokens.push_back(std::move(tok));
  }
  tokens.emplace_back(TokenKind::End, "", src_.size());
  return tokens;
}

}  // namespace walterdb
