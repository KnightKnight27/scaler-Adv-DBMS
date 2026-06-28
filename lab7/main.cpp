#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stack>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <cctype>

// ─────────────────────────────────────────────
//  Token types
// ─────────────────────────────────────────────
enum class TokType {
    NUMBER, STRING, IDENT,
    OP,                             // > < >= <= = !=
    AND, OR, NOT,
    SELECT, FROM, WHERE,
    STAR, COMMA,
    LPAREN, RPAREN,
    END
};

struct Token {
    TokType     type;
    std::string val;
};

// ─────────────────────────────────────────────
//  Lexer
// ─────────────────────────────────────────────
static std::string toUpper(std::string s) {
    for (auto& c : s) c = (char)toupper(c);
    return s;
}

std::vector<Token> tokenize(const std::string& src) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < src.size()) {
        if (isspace(src[i])) { i++; continue; }

        if (src[i] == '\'') {
            size_t j = src.find('\'', i + 1);
            if (j == std::string::npos) throw std::runtime_error("unterminated string");
            out.push_back({TokType::STRING, src.substr(i + 1, j - i - 1)});
            i = j + 1; continue;
        }

        if (i + 1 < src.size()) {
            std::string two = src.substr(i, 2);
            if (two == ">=" || two == "<=" || two == "!=") {
                out.push_back({TokType::OP, two}); i += 2; continue;
            }
        }

        if (src[i] == '>' || src[i] == '<' || src[i] == '=') {
            out.push_back({TokType::OP, {src[i]}}); i++; continue;
        }
        if (src[i] == '(') { out.push_back({TokType::LPAREN, "("}); i++; continue; }
        if (src[i] == ')') { out.push_back({TokType::RPAREN, ")"}); i++; continue; }
        if (src[i] == '*') { out.push_back({TokType::STAR,   "*"}); i++; continue; }
        if (src[i] == ',') { out.push_back({TokType::COMMA,  ","}); i++; continue; }

        if (isdigit(src[i]) || (src[i] == '-' && i+1 < src.size() && isdigit(src[i+1]))) {
            size_t j = i + (src[i] == '-' ? 1 : 0);
            while (j < src.size() && (isdigit(src[j]) || src[j] == '.')) j++;
            out.push_back({TokType::NUMBER, src.substr(i, j - i)});
            i = j; continue;
        }

        if (isalpha(src[i]) || src[i] == '_') {
            size_t j = i;
            while (j < src.size() && (isalnum(src[j]) || src[j] == '_')) j++;
            std::string word = src.substr(i, j - i);
            std::string up   = toUpper(word);
            TokType t = TokType::IDENT;
            if      (up == "SELECT") t = TokType::SELECT;
            else if (up == "FROM")   t = TokType::FROM;
            else if (up == "WHERE")  t = TokType::WHERE;
            else if (up == "AND")    t = TokType::AND;
            else if (up == "OR")     t = TokType::OR;
            else if (up == "NOT")    t = TokType::NOT;
            out.push_back({t, word});
            i = j; continue;
        }

        throw std::runtime_error(std::string("unexpected char: ") + src[i]);
    }
    out.push_back({TokType::END, ""});
    return out;
}

// ─────────────────────────────────────────────
//  Shunting-Yard  (infix WHERE → postfix RPN)
//  Precedence: comparisons(3) > AND(2) > OR(1)
// ─────────────────────────────────────────────
static int prec(const Token& t) {
    if (t.type == TokType::OP)  return 3;
    if (t.type == TokType::AND) return 2;
    if (t.type == TokType::OR)  return 1;
    return 0;
}

static bool isOp(const Token& t) {
    return t.type == TokType::OP  ||
           t.type == TokType::AND ||
           t.type == TokType::OR;
}

std::vector<Token> extractWhere(const std::vector<Token>& tokens) {
    size_t i = 0;
    while (i < tokens.size() && tokens[i].type != TokType::WHERE) i++;
    if (i == tokens.size()) return {};
    return std::vector<Token>(tokens.begin() + i + 1, tokens.end());
}

std::vector<Token> shuntingYard(const std::vector<Token>& where) {
    std::vector<Token> output;
    std::stack<Token>  ops;

    for (const auto& tok : where) {
        if (tok.type == TokType::END) break;

        if (tok.type == TokType::NUMBER ||
            tok.type == TokType::STRING ||
            tok.type == TokType::IDENT) {
            output.push_back(tok);
        } else if (isOp(tok)) {
            while (!ops.empty() && isOp(ops.top()) && prec(ops.top()) >= prec(tok)) {
                output.push_back(ops.top()); ops.pop();
            }
            ops.push(tok);
        } else if (tok.type == TokType::LPAREN) {
            ops.push(tok);
        } else if (tok.type == TokType::RPAREN) {
            while (!ops.empty() && ops.top().type != TokType::LPAREN) {
                output.push_back(ops.top()); ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("mismatched parentheses");
            ops.pop();
        }
    }
    while (!ops.empty()) {
        if (ops.top().type == TokType::LPAREN)
            throw std::runtime_error("mismatched parentheses");
        output.push_back(ops.top()); ops.pop();
    }
    return output;
}

// ─────────────────────────────────────────────
//  Row type  (column → string value)
// ─────────────────────────────────────────────
using Row = std::unordered_map<std::string, std::string>;

// ─────────────────────────────────────────────
//  Postfix evaluator
// ─────────────────────────────────────────────
static double toNum(const std::string& s) { return std::stod(s); }
static bool   isNum(const std::string& s) {
    try { std::stod(s); return true; } catch (...) { return false; }
}

bool evalPostfix(const std::vector<Token>& rpn, const Row& row) {
    std::stack<std::string> st;
    for (const auto& tok : rpn) {
        if (tok.type == TokType::NUMBER || tok.type == TokType::STRING) {
            st.push(tok.val);
        } else if (tok.type == TokType::IDENT) {
            auto it = row.find(tok.val);
            if (it == row.end()) throw std::runtime_error("unknown column: " + tok.val);
            st.push(it->second);
        } else if (tok.type == TokType::OP) {
            if (st.size() < 2) throw std::runtime_error("bad expression");
            std::string rhs = st.top(); st.pop();
            std::string lhs = st.top(); st.pop();
            bool result = false;
            if (isNum(lhs) && isNum(rhs)) {
                double l = toNum(lhs), r = toNum(rhs);
                if      (tok.val == ">")  result = l >  r;
                else if (tok.val == "<")  result = l <  r;
                else if (tok.val == ">=") result = l >= r;
                else if (tok.val == "<=") result = l <= r;
                else if (tok.val == "=")  result = l == r;
                else if (tok.val == "!=") result = l != r;
            } else {
                if      (tok.val == "=")  result = lhs == rhs;
                else if (tok.val == "!=") result = lhs != rhs;
                else if (tok.val == ">")  result = lhs >  rhs;
                else if (tok.val == "<")  result = lhs <  rhs;
                else if (tok.val == ">=") result = lhs >= rhs;
                else if (tok.val == "<=") result = lhs <= rhs;
            }
            st.push(result ? "1" : "0");
        } else if (tok.type == TokType::AND) {
            std::string r = st.top(); st.pop();
            std::string l = st.top(); st.pop();
            st.push((l == "1" && r == "1") ? "1" : "0");
        } else if (tok.type == TokType::OR) {
            std::string r = st.top(); st.pop();
            std::string l = st.top(); st.pop();
            st.push((l == "1" || r == "1") ? "1" : "0");
        }
    }
    return !st.empty() && st.top() == "1";
}

// ─────────────────────────────────────────────
//  SELECT column extraction
// ─────────────────────────────────────────────
std::vector<std::string> extractColumns(const std::vector<Token>& tokens) {
    std::vector<std::string> cols;
    size_t i = 0;
    while (i < tokens.size() && tokens[i].type != TokType::SELECT) i++;
    i++;
    if (i < tokens.size() && tokens[i].type == TokType::STAR) return {};
    while (i < tokens.size() && tokens[i].type != TokType::FROM && tokens[i].type != TokType::END) {
        if (tokens[i].type == TokType::IDENT) cols.push_back(tokens[i].val);
        i++;
    }
    return cols;
}

// ─────────────────────────────────────────────
//  Print helpers
// ─────────────────────────────────────────────
void printRow(const Row& row, const std::vector<std::string>& cols,
              const std::vector<std::string>& allCols) {
    const auto& display = cols.empty() ? allCols : cols;
    for (size_t i = 0; i < display.size(); i++) {
        if (i) std::cout << " | ";
        auto it = row.find(display[i]);
        std::cout << display[i] << ": " << (it != row.end() ? it->second : "NULL");
    }
    std::cout << "\n";
}

void printRPN(const std::vector<Token>& rpn) {
    std::cout << "  RPN: ";
    for (auto& t : rpn) std::cout << t.val << " ";
    std::cout << "\n";
}

// ─────────────────────────────────────────────
//  Run a query against a table
// ─────────────────────────────────────────────
void runQuery(const std::string& query,
              const std::vector<Row>& table,
              const std::vector<std::string>& allCols) {
    std::cout << "\n>>> " << query << "\n";
    auto tokens  = tokenize(query);
    auto selCols = extractColumns(tokens);
    auto where   = extractWhere(tokens);

    std::vector<Token> rpn;
    bool hasWhere = !where.empty();
    if (hasWhere) { rpn = shuntingYard(where); printRPN(rpn); }

    std::cout << "Results:\n";
    int count = 0;
    for (const auto& row : table) {
        if (!hasWhere || evalPostfix(rpn, row)) {
            printRow(row, selCols, allCols);
            count++;
        }
    }
    std::cout << "(" << count << " row" << (count != 1 ? "s" : "") << ")\n";
}

// ─────────────────────────────────────────────
//  main — sample dataset + queries
// ─────────────────────────────────────────────
int main() {
    std::vector<std::string> empCols = {"id", "name", "dept", "salary", "age"};
    std::vector<Row> employees = {
        {{"id","1"},{"name","Alice"},  {"dept","Engineering"},{"salary","95000"},{"age","30"}},
        {{"id","2"},{"name","Bob"},    {"dept","Marketing"},  {"salary","72000"},{"age","45"}},
        {{"id","3"},{"name","Carol"},  {"dept","Engineering"},{"salary","110000"},{"age","35"}},
        {{"id","4"},{"name","Dave"},   {"dept","HR"},         {"salary","60000"},{"age","28"}},
        {{"id","5"},{"name","Eve"},    {"dept","Engineering"},{"salary","88000"},{"age","40"}},
        {{"id","6"},{"name","Frank"},  {"dept","Marketing"},  {"salary","95000"},{"age","52"}},
        {{"id","7"},{"name","Grace"},  {"dept","HR"},         {"salary","67000"},{"age","31"}},
        {{"id","8"},{"name","Heidi"},  {"dept","Engineering"},{"salary","120000"},{"age","38"}},
    };

    std::cout << "=== Lab 7: SQL Tokenizer + Shunting-Yard Postfix Evaluator ===\n";

    runQuery("SELECT name, salary FROM employees WHERE salary > 90000",
             employees, empCols);

    runQuery("SELECT name, dept, age FROM employees WHERE dept = 'Engineering' AND age < 36",
             employees, empCols);

    runQuery("SELECT name, dept FROM employees WHERE dept = 'HR' OR dept = 'Marketing'",
             employees, empCols);

    runQuery("SELECT name, salary, dept FROM employees WHERE (dept = 'Engineering' OR dept = 'Marketing') AND salary >= 95000",
             employees, empCols);

    runQuery("SELECT name, age FROM employees WHERE age >= 30 AND age <= 40",
             employees, empCols);

    runQuery("SELECT name, dept FROM employees WHERE dept != 'Engineering'",
             employees, empCols);

    runQuery("SELECT * FROM employees",
             employees, empCols);

    return 0;
}
