#include "tokenizer.h"

#include <cctype>
#include <set>
#include <stdexcept>

namespace minidb {

namespace {
const std::set<std::string>& Keywords() {
    static const std::set<std::string> kw = {
        "CREATE", "TABLE", "INDEX", "ON", "UNIQUE", "INSERT", "INTO", "VALUES",
        "SELECT", "FROM", "WHERE", "JOIN", "INNER", "DELETE", "AND", "OR",
        "FOR", "UPDATE", "BEGIN", "COMMIT", "ABORT", "ROLLBACK", "PRIMARY", "KEY",
        "INT", "INTEGER", "BIGINT", "VARCHAR", "TEXT", "STRING", "BOOL", "BOOLEAN",
        "TRUE", "FALSE"};
    return kw;
}

std::string Upper(const std::string& s) {
    std::string r;
    for (char c : s) r.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return r;
}
}  // namespace

std::vector<Token> Tokenize(const std::string& sql) {
    std::vector<Token> toks;
    size_t i = 0, n = sql.size();
    while (i < n) {
        char c = sql[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ';') { ++i; continue; }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(sql[j])) || sql[j] == '_')) ++j;
            std::string word = sql.substr(i, j - i);
            std::string up = Upper(word);
            if (Keywords().count(up)) toks.push_back({TokKind::Keyword, up, word});
            else                      toks.push_back({TokKind::Ident, up, word});
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
            size_t j = i + (c == '-' ? 1 : 0);
            while (j < n && std::isdigit(static_cast<unsigned char>(sql[j]))) ++j;
            std::string num = sql.substr(i, j - i);
            toks.push_back({TokKind::Number, num, num});
            i = j;
            continue;
        }
        if (c == '\'') {  // string literal
            size_t j = i + 1;
            std::string val;
            while (j < n && sql[j] != '\'') val.push_back(sql[j++]);
            if (j >= n) throw std::runtime_error("unterminated string literal");
            ++j;  // closing quote
            toks.push_back({TokKind::String, val, val});
            i = j;
            continue;
        }
        // multi-char operators
        if ((c == '<' || c == '>' || c == '!') && i + 1 < n && sql[i + 1] == '=') {
            toks.push_back({TokKind::Op, sql.substr(i, 2), sql.substr(i, 2)});
            i += 2;
            continue;
        }
        if (c == '=' || c == '<' || c == '>') {
            toks.push_back({TokKind::Op, std::string(1, c), std::string(1, c)});
            ++i;
            continue;
        }
        if (c == '(' || c == ')' || c == ',' || c == '.' || c == '*') {
            toks.push_back({TokKind::Punct, std::string(1, c), std::string(1, c)});
            ++i;
            continue;
        }
        throw std::runtime_error(std::string("unexpected character: ") + c);
    }
    toks.push_back({TokKind::End, "", ""});
    return toks;
}

}  // namespace minidb
