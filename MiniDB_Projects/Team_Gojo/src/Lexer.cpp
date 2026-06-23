#include "Lexer.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>

// ── Keyword table ───────────────────────────────────────────────────────
// Maps uppercase keyword strings to their token types.
// We use an unordered_map for O(1) lookup during tokenization.

static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"SELECT", TokenType::SELECT},
    {"INSERT", TokenType::INSERT},
    {"DELETE", TokenType::DELETE},
    {"FROM", TokenType::FROM},
    {"WHERE", TokenType::WHERE},
    {"JOIN", TokenType::JOIN},
    {"ON", TokenType::ON},
    {"INTO", TokenType::INTO},
    {"VALUES", TokenType::VALUES},
    {"AND", TokenType::AND},
    {"SET", TokenType::SET},
    {"CREATE", TokenType::CREATE},
    {"TABLE", TokenType::TABLE},
    {"BEGIN", TokenType::BEGIN_TXN},
    {"COMMIT", TokenType::COMMIT_TXN},
    {"ROLLBACK", TokenType::ROLLBACK_TXN},
};

// ── Constructor ─────────────────────────────────────────────────────────

Lexer::Lexer(const std::string &input) : input_(input), pos_(0) {}

// ── Core tokenization ───────────────────────────────────────────────────

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  while (pos_ < static_cast<int>(input_.size())) {
    skipWhitespace();

    if (pos_ >= static_cast<int>(input_.size()))
      break;

    char c = currentChar();
    int startPos = pos_;

    // ── Single-character tokens ─────────────────────────────────
    switch (c) {
    case '*':
      tokens.emplace_back(TokenType::STAR, "*", startPos);
      advance();
      continue;
    case ',':
      tokens.emplace_back(TokenType::COMMA, ",", startPos);
      advance();
      continue;
    case '(':
      tokens.emplace_back(TokenType::LPAREN, "(", startPos);
      advance();
      continue;
    case ')':
      tokens.emplace_back(TokenType::RPAREN, ")", startPos);
      advance();
      continue;
    case ';':
      tokens.emplace_back(TokenType::SEMICOLON, ";", startPos);
      advance();
      continue;
    case '.':
      tokens.emplace_back(TokenType::DOT, ".", startPos);
      advance();
      continue;
    case '=':
      tokens.emplace_back(TokenType::EQUALS, "=", startPos);
      advance();
      continue;
    case '<':
      tokens.emplace_back(TokenType::LESS, "<", startPos);
      advance();
      continue;
    case '>':
      tokens.emplace_back(TokenType::GREATER, ">", startPos);
      advance();
      continue;
    default:
      break;
    }

    // ── Integer literals ────────────────────────────────────────
    // We also handle negative numbers (e.g., -1 for delete markers)
    if (std::isdigit(c) ||
        (c == '-' && pos_ + 1 < static_cast<int>(input_.size()) &&
         std::isdigit(input_[pos_ + 1]))) {
      tokens.push_back(readNumber());
      continue;
    }

    // ── Identifiers and keywords ────────────────────────────────
    if (std::isalpha(c) || c == '_') {
      tokens.push_back(readIdentifierOrKeyword());
      continue;
    }

    throw std::runtime_error("Lexer error: unexpected character '" +
                             std::string(1, c) + "' at position " +
                             std::to_string(pos_));
  }

  // Always end with EOF
  tokens.emplace_back(TokenType::EOF_TOKEN, "", pos_);
  return tokens;
}

// ── Character helpers ───────────────────────────────────────────────────

char Lexer::currentChar() const { return input_[pos_]; }

char Lexer::peek() const {
  if (pos_ + 1 < static_cast<int>(input_.size())) {
    return input_[pos_ + 1];
  }
  return '\0';
}

void Lexer::advance() { pos_++; }

void Lexer::skipWhitespace() {
  while (pos_ < static_cast<int>(input_.size()) && std::isspace(input_[pos_])) {
    pos_++;
  }
}

// ── Multi-character token readers ───────────────────────────────────────

Token Lexer::readNumber() {
  int startPos = pos_;
  std::string num;

  // Handle negative sign
  if (currentChar() == '-') {
    num += '-';
    advance();
  }

  while (pos_ < static_cast<int>(input_.size()) &&
         std::isdigit(currentChar())) {
    num += currentChar();
    advance();
  }

  return Token(TokenType::INT_LITERAL, num, startPos);
}

Token Lexer::readIdentifierOrKeyword() {
  int startPos = pos_;
  std::string word;

  // Identifiers can contain letters, digits, and underscores
  while (pos_ < static_cast<int>(input_.size()) &&
         (std::isalnum(currentChar()) || currentChar() == '_')) {
    word += currentChar();
    advance();
  }

  // Check if the word is a SQL keyword (case-insensitive comparison)
  std::string upper = word;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

  auto it = KEYWORDS.find(upper);
  if (it != KEYWORDS.end()) {
    return Token(it->second, upper, startPos);
  }

  // Not a keyword — it's an identifier (table name, column name)
  // We preserve the original case for identifiers
  return Token(TokenType::IDENTIFIER, word, startPos);
}
