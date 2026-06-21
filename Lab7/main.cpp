#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <stack>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <iomanip>

using Row = std::unordered_map<std::string, std::string>;

enum class TokenKind { IDENT, NUM, STR, OP, LPAREN, RPAREN, KW };

struct Token {
    TokenKind kind;
    std::string lexeme;
};

std::vector<Token> lex(const std::string& src) {
    std::vector<Token> tokens;
    size_t pos = 0;

    while (pos < src.size()) {
        if (std::isspace(src[pos])) { ++pos; continue; }

        if (src[pos] == '(') { tokens.push_back({TokenKind::LPAREN, "("}); ++pos; continue; }
        if (src[pos] == ')') { tokens.push_back({TokenKind::RPAREN, ")"}); ++pos; continue; }

        if (src[pos] == '<' || src[pos] == '>' || src[pos] == '!' || src[pos] == '=') {
            std::string op(1, src[pos]);
            if (pos + 1 < src.size() && src[pos + 1] == '=') { op += '='; ++pos; }
            tokens.push_back({TokenKind::OP, op});
            ++pos;
            continue;
        }

        if (src[pos] == '\'') {
            std::string lit;
            ++pos;
            while (pos < src.size() && src[pos] != '\'') lit += src[pos++];
            ++pos;
            tokens.push_back({TokenKind::STR, lit});
            continue;
        }

        if (std::isdigit(src[pos])) {
            std::string num;
            while (pos < src.size() && (std::isdigit(src[pos]) || src[pos] == '.')) num += src[pos++];
            tokens.push_back({TokenKind::NUM, num});
            continue;
        }

        if (std::isalpha(src[pos]) || src[pos] == '_' || src[pos] == '*') {
            std::string word;
            if (src[pos] == '*') { tokens.push_back({TokenKind::IDENT, "*"}); ++pos; continue; }
            while (pos < src.size() && (std::isalnum(src[pos]) || src[pos] == '_')) word += src[pos++];
            std::string upper = word;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            if (upper == "SELECT" || upper == "FROM" || upper == "WHERE" ||
                upper == "AND" || upper == "OR" || upper == "NOT") {
                tokens.push_back({TokenKind::KW, upper});
            } else {
                tokens.push_back({TokenKind::IDENT, word});
            }
            continue;
        }

        ++pos;
    }
    return tokens;
}

int precedence(const std::string& op) {
    if (op == "OR")  return 1;
    if (op == "AND") return 2;
    if (op == "NOT") return 3;
    return 4;
}

std::vector<Token> toRPN(const std::vector<Token>& clause) {
    std::vector<Token> output;
    std::stack<Token> ops;

    for (const auto& tok : clause) {
        switch (tok.kind) {
        case TokenKind::IDENT:
        case TokenKind::NUM:
        case TokenKind::STR:
            output.push_back(tok);
            break;

        case TokenKind::OP:
        case TokenKind::KW: {
            while (!ops.empty() &&
                   ops.top().kind != TokenKind::LPAREN &&
                   precedence(ops.top().lexeme) >= precedence(tok.lexeme)) {
                output.push_back(ops.top());
                ops.pop();
            }
            ops.push(tok);
            break;
        }

        case TokenKind::LPAREN:
            ops.push(tok);
            break;

        case TokenKind::RPAREN:
            while (!ops.empty() && ops.top().kind != TokenKind::LPAREN) {
                output.push_back(ops.top());
                ops.pop();
            }
            if (!ops.empty()) ops.pop();
            break;
        }
    }

    while (!ops.empty()) {
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

bool evalRPN(const std::vector<Token>& rpn, const Row& row) {
    if (rpn.empty()) return true;

    std::stack<std::string> stk;

    for (const auto& tok : rpn) {
        if (tok.kind == TokenKind::NUM || tok.kind == TokenKind::STR) {
            stk.push(tok.lexeme);
        } else if (tok.kind == TokenKind::IDENT) {
            auto it = row.find(tok.lexeme);
            if (it == row.end()) throw std::runtime_error("Unknown column: " + tok.lexeme);
            stk.push(it->second);
        } else {
            if (stk.size() < 2) continue;
            std::string rhs = stk.top(); stk.pop();
            std::string lhs = stk.top(); stk.pop();

            auto cmpNum = [&](auto fn) -> std::string {
                return fn(std::stod(lhs), std::stod(rhs)) ? "1" : "0";
            };

            if      (tok.lexeme == "=")  stk.push(lhs == rhs ? "1" : "0");
            else if (tok.lexeme == "!=") stk.push(lhs != rhs ? "1" : "0");
            else if (tok.lexeme == ">")  stk.push(cmpNum(std::greater<double>{}));
            else if (tok.lexeme == "<")  stk.push(cmpNum(std::less<double>{}));
            else if (tok.lexeme == ">=") stk.push(cmpNum(std::greater_equal<double>{}));
            else if (tok.lexeme == "<=") stk.push(cmpNum(std::less_equal<double>{}));
            else if (tok.lexeme == "AND") stk.push((lhs == "1" && rhs == "1") ? "1" : "0");
            else if (tok.lexeme == "OR")  stk.push((lhs == "1" || rhs == "1") ? "1" : "0");
        }
    }

    return !stk.empty() && stk.top() == "1";
}

void runSelect(const std::string& query, const std::vector<Row>& table) {
    auto tokens = lex(query);

    std::vector<std::string> cols;
    std::vector<Token> whereClause;

    size_t i = 0;
    if (i < tokens.size() && tokens[i].lexeme == "SELECT") ++i;

    while (i < tokens.size() && tokens[i].lexeme != "FROM") {
        if (tokens[i].kind == TokenKind::IDENT) cols.push_back(tokens[i].lexeme);
        ++i;
    }
    if (i < tokens.size()) ++i;
    if (i < tokens.size()) ++i;

    if (i < tokens.size() && tokens[i].lexeme == "WHERE") {
        ++i;
        while (i < tokens.size()) whereClause.push_back(tokens[i++]);
    }

    auto rpn = toRPN(whereClause);

    std::vector<std::string> header;
    if (!cols.empty() && cols[0] == "*") {
        if (!table.empty())
            for (const auto& kv : table[0]) header.push_back(kv.first);
    } else {
        header = cols;
    }

    for (const auto& h : header)
        std::cout << std::left << std::setw(14) << h;
    std::cout << "\n" << std::string(14 * header.size(), '-') << "\n";

    for (const auto& row : table) {
        if (evalRPN(rpn, row)) {
            for (const auto& h : header) {
                auto it = row.find(h);
                std::cout << std::left << std::setw(14)
                          << (it != row.end() ? it->second : "NULL");
            }
            std::cout << "\n";
        }
    }
    std::cout << "\n";
}

int main() {
    std::vector<Row> employees = {
        {{"id","1"},{"name","Alice"},  {"dept","Engineering"},{"salary","95000"}, {"age","24"}},
        {{"id","2"},{"name","Bob"},    {"dept","Marketing"},  {"salary","72000"}, {"age","29"}},
        {{"id","3"},{"name","Carol"},  {"dept","Engineering"},{"salary","110000"},{"age","31"}},
        {{"id","4"},{"name","Dave"},   {"dept","HR"},         {"salary","68000"}, {"age","26"}},
        {{"id","5"},{"name","Eve"},    {"dept","Engineering"},{"salary","98000"}, {"age","28"}},
        {{"id","6"},{"name","Frank"},  {"dept","Marketing"},  {"salary","80000"}, {"age","33"}},
        {{"id","7"},{"name","Grace"},  {"dept","HR"},         {"salary","75000"}, {"age","35"}},
    };

    struct Case { std::string label; std::string query; };
    std::vector<Case> cases = {
        {"Engineers earning > 96000",
         "SELECT name, salary FROM employees WHERE dept = 'Engineering' AND salary > 96000"},

        {"Non-HR employees",
         "SELECT name, dept FROM employees WHERE dept != 'HR'"},

        {"Employees aged between 25 and 30 (inclusive)",
         "SELECT name, age, dept FROM employees WHERE age >= 25 AND age <= 30"},

        {"Marketing or HR with salary >= 72000",
         "SELECT name, dept, salary FROM employees "
         "WHERE (dept = 'Marketing' OR dept = 'HR') AND salary >= 72000"},

        {"All employees (wildcard SELECT *)",
         "SELECT * FROM employees"},
    };

    for (const auto& c : cases) {
        std::cout << "=== " << c.label << " ===\n";
        std::cout << "SQL: " << c.query << "\n\n";
        runSelect(c.query, employees);
    }

    return 0;
}
