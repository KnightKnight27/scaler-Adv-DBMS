#include "lexer.hpp"
#include <cctype>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

namespace minidb {

static const std::unordered_map<std::string, Tok>& keywords() {
    static const std::unordered_map<std::string, Tok> kw = {
        {"CREATE", Tok::CREATE}, {"TABLE", Tok::TABLE}, {"INSERT", Tok::INSERT},
        {"INTO", Tok::INTO}, {"VALUES", Tok::VALUES}, {"SELECT", Tok::SELECT},
        {"FROM", Tok::FROM}, {"WHERE", Tok::WHERE}, {"AND", Tok::AND},
        {"OR", Tok::OR}, {"DELETE", Tok::DELETE}, {"JOIN", Tok::JOIN},
        {"ON", Tok::ON}, {"BEGIN", Tok::BEGIN}, {"COMMIT", Tok::COMMIT},
        {"ROLLBACK", Tok::ROLLBACK}, {"COUNT", Tok::COUNT},
        {"INT", Tok::KW_INT}, {"TEXT", Tok::KW_TEXT},
    };
    return kw;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    while (pos_ < src_.size()) {
        char c = peek();
        if (std::isspace(static_cast<unsigned char>(c))) { get(); continue; }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string word;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
                word += get();
            std::string upper = word;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            auto it = keywords().find(upper);
            if (it != keywords().end()) out.push_back({it->second, upper, 0});
            else out.push_back({Tok::IDENT, word, 0});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && std::isdigit(static_cast<unsigned char>(pos_ + 1 < src_.size() ? src_[pos_ + 1] : '\0')))) {
            std::string num;
            num += get(); // first digit or sign
            while (std::isdigit(static_cast<unsigned char>(peek()))) num += get();
            out.push_back({Tok::INT_LIT, num, std::stoll(num)});
            continue;
        }
        if (c == '\'') {
            get(); // opening quote
            std::string s;
            while (peek() != '\'' && peek() != '\0') s += get();
            if (get() != '\'') throw std::runtime_error("unterminated string literal");
            out.push_back({Tok::STR_LIT, s, 0});
            continue;
        }

        // Punctuation and operators.
        get();
        switch (c) {
            case '(': out.push_back({Tok::LPAREN, "(", 0}); break;
            case ')': out.push_back({Tok::RPAREN, ")", 0}); break;
            case ',': out.push_back({Tok::COMMA, ",", 0}); break;
            case ';': out.push_back({Tok::SEMI, ";", 0}); break;
            case '*': out.push_back({Tok::STAR, "*", 0}); break;
            case '.': out.push_back({Tok::DOT, ".", 0}); break;
            case '=': out.push_back({Tok::EQ, "=", 0}); break;
            case '<':
                if (peek() == '=') { get(); out.push_back({Tok::LE, "<=", 0}); }
                else if (peek() == '>') { get(); out.push_back({Tok::NE, "<>", 0}); }
                else out.push_back({Tok::LT, "<", 0});
                break;
            case '>':
                if (peek() == '=') { get(); out.push_back({Tok::GE, ">=", 0}); }
                else out.push_back({Tok::GT, ">", 0});
                break;
            case '!':
                if (peek() == '=') { get(); out.push_back({Tok::NE, "!=", 0}); }
                else throw std::runtime_error("unexpected '!'");
                break;
            default:
                throw std::runtime_error(std::string("unexpected character: ") + c);
        }
    }
    out.push_back({Tok::END, "", 0});
    return out;
}

} // namespace minidb
