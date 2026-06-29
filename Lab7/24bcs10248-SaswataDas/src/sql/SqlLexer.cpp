#include "sql/SqlLexer.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace sql {

namespace {

std::string toUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

}  // namespace

std::vector<SqlToken> SqlLexer::tokenize(const std::string& input) const {
    std::vector<SqlToken> tokens;
    size_t i = 0;
    const size_t n = input.size();

    while (i < n) {
        char c = input[i];

        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }

        if (c == ',') { tokens.push_back({SqlTokenType::Comma, ",", 0.0}); ++i; continue; }
        if (c == '*') { tokens.push_back({SqlTokenType::Star, "*", 0.0}); ++i; continue; }
        if (c == '(') { tokens.push_back({SqlTokenType::LeftParen, "(", 0.0}); ++i; continue; }
        if (c == ')') { tokens.push_back({SqlTokenType::RightParen, ")", 0.0}); ++i; continue; }

        // Comparison operators, including the two-character forms.
        if (c == '=' || c == '!' || c == '<' || c == '>') {
            std::string sym(1, c);
            char next = (i + 1 < n) ? input[i + 1] : '\0';
            if ((c == '!' && next == '=') ||
                (c == '<' && (next == '=' || next == '>')) ||
                (c == '>' && next == '=')) {
                sym += next;
                ++i;
            } else if (c == '!') {
                throw std::runtime_error("unexpected '!' (did you mean '!='?)");
            }
            tokens.push_back({SqlTokenType::Comparison, sym, 0.0});
            ++i;
            continue;
        }

        // String literal in single quotes.
        if (c == '\'') {
            ++i;  // skip opening quote
            std::string body;
            bool closed = false;
            while (i < n) {
                if (input[i] == '\'') { closed = true; ++i; break; }
                body += input[i];
                ++i;
            }
            if (!closed) throw std::runtime_error("unterminated string literal");
            tokens.push_back({SqlTokenType::String, body, 0.0});
            continue;
        }

        // Numeric literal.
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t start = i;
            bool seenDot = false;
            while (i < n && (std::isdigit(static_cast<unsigned char>(input[i])) || input[i] == '.')) {
                if (input[i] == '.') {
                    if (seenDot) throw std::runtime_error("malformed number literal");
                    seenDot = true;
                }
                ++i;
            }
            std::string text = input.substr(start, i - start);
            tokens.push_back({SqlTokenType::Number, text, std::stod(text)});
            continue;
        }

        // Identifier or keyword.
        if (isIdentStart(c)) {
            size_t start = i;
            while (i < n && isIdentPart(input[i])) ++i;
            std::string word = input.substr(start, i - start);
            std::string upper = toUpper(word);
            if (upper == "SELECT") tokens.push_back({SqlTokenType::Select, word, 0.0});
            else if (upper == "FROM") tokens.push_back({SqlTokenType::From, word, 0.0});
            else if (upper == "WHERE") tokens.push_back({SqlTokenType::Where, word, 0.0});
            else if (upper == "AND") tokens.push_back({SqlTokenType::And, word, 0.0});
            else if (upper == "OR") tokens.push_back({SqlTokenType::Or, word, 0.0});
            else tokens.push_back({SqlTokenType::Identifier, word, 0.0});
            continue;
        }

        throw std::runtime_error(std::string("unexpected character in query: ") + c);
    }

    tokens.push_back({SqlTokenType::End, "", 0.0});
    return tokens;
}

}  // namespace sql
