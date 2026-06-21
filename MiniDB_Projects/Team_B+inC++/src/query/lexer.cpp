#include "lexer.hpp"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace {
const std::unordered_map<std::string, Tok>& keywords() {
    static const std::unordered_map<std::string, Tok> kw = {
        {"SELECT", Tok::SELECT}, {"FROM", Tok::FROM}, {"WHERE", Tok::WHERE},
        {"INSERT", Tok::INSERT}, {"INTO", Tok::INTO}, {"VALUES", Tok::VALUES},
        {"DELETE", Tok::DELETE}, {"CREATE", Tok::CREATE}, {"TABLE", Tok::TABLE},
        {"JOIN", Tok::JOIN}, {"ON", Tok::ON}, {"AND", Tok::AND}, {"OR", Tok::OR},
        {"INT", Tok::INT_KW}, {"TEXT", Tok::TEXT_KW},
        {"PRIMARY", Tok::PRIMARY}, {"KEY", Tok::KEY},
        {"BEGIN", Tok::BEGIN}, {"COMMIT", Tok::COMMIT}, {"ROLLBACK", Tok::ROLLBACK},
    };
    return kw;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    const std::size_t n = sql_.size();

    while (pos_ < n) {
        char c = sql_[pos_];

        if (std::isspace(static_cast<unsigned char>(c))) { ++pos_; continue; }

        // ident or keyword
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t start = pos_;
            while (pos_ < n && (std::isalnum(static_cast<unsigned char>(sql_[pos_])) || sql_[pos_] == '_'))
                ++pos_;
            std::string word = sql_.substr(start, pos_ - start);
            auto it = keywords().find(upper(word));
            if (it != keywords().end()) out.push_back({it->second, word});
            else                        out.push_back({Tok::IDENT, word});
            continue;
        }

        // number
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t start = pos_;
            while (pos_ < n && std::isdigit(static_cast<unsigned char>(sql_[pos_]))) ++pos_;
            out.push_back({Tok::NUMBER, sql_.substr(start, pos_ - start)});
            continue;
        }

        // string literal
        if (c == '\'') {
            ++pos_;
            std::size_t start = pos_;
            while (pos_ < n && sql_[pos_] != '\'') ++pos_;
            if (pos_ >= n) throw std::runtime_error("unterminated string literal");
            std::string s = sql_.substr(start, pos_ - start);
            ++pos_;
            out.push_back({Tok::STRING, s});
            continue;
        }

        // operators / punctuation
        switch (c) {
            case '(': out.push_back({Tok::LPAREN, "("}); ++pos_; break;
            case ')': out.push_back({Tok::RPAREN, ")"}); ++pos_; break;
            case ',': out.push_back({Tok::COMMA,  ","}); ++pos_; break;
            case ';': out.push_back({Tok::SEMI,   ";"}); ++pos_; break;
            case '.': out.push_back({Tok::DOT,    "."}); ++pos_; break;
            case '*': out.push_back({Tok::STAR,   "*"}); ++pos_; break;
            case '=': out.push_back({Tok::EQ,     "="}); ++pos_; break;
            case '<':
                if (pos_ + 1 < n && sql_[pos_ + 1] == '=') { out.push_back({Tok::LE, "<="}); pos_ += 2; }
                else { out.push_back({Tok::LT, "<"}); ++pos_; }
                break;
            case '>':
                if (pos_ + 1 < n && sql_[pos_ + 1] == '=') { out.push_back({Tok::GE, ">="}); pos_ += 2; }
                else { out.push_back({Tok::GT, ">"}); ++pos_; }
                break;
            case '!':
                if (pos_ + 1 < n && sql_[pos_ + 1] == '=') { out.push_back({Tok::NEQ, "!="}); pos_ += 2; }
                else throw std::runtime_error("unexpected '!' (did you mean '!='?)");
                break;
            default:
                throw std::runtime_error(std::string("unexpected character: ") + c);
        }
    }

    out.push_back({Tok::END, ""});
    return out;
}
