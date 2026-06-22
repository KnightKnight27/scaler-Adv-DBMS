#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cctype>
#include <variant>
#include <algorithm>
#include <functional>
#include <cmath>

// Operator metadata
struct OpInfo {
    int precedence;
    bool right_assoc;
};

const std::unordered_map<std::string, OpInfo> OPS = {
    {"OR", {1, false}},
    {"AND", {2, false}},
    {"==", {3, false}},
    {"!=", {3, false}},
    {">",  {3, false}},
    {"<",  {3, false}},
    {">=", {3, false}},
    {"<=", {3, false}}
};

// Case-insensitive string helper
std::string to_upper(std::string s) {
    for (auto& c : s) c = std::toupper(c);
    return s;
}

// Tokenize: strings (single quoted), numbers, identifiers, operators, parens
std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    int i = 0, n = expr.size();
    while (i < n) {
        if (std::isspace(expr[i])) {
            i++;
            continue;
        }
        
        // Single quoted string literal (e.g. 'Mumbai')
        if (expr[i] == '\'') {
            int start = i;
            i++;
            while (i < n && expr[i] != '\'') {
                i++;
            }
            if (i < n) {
                i++; // consume closing quote
                tokens.push_back(expr.substr(start, i - start));
            } else {
                throw std::runtime_error("Unclosed string literal");
            }
            continue;
        }

        // Numeric literals
        if (std::isdigit(expr[i]) || (expr[i] == '.' && i + 1 < n && std::isdigit(expr[i+1]))) {
            int j = i;
            while (j < n && (std::isdigit(expr[j]) || expr[j] == '.')) {
                j++;
            }
            tokens.push_back(expr.substr(i, j - i));
            i = j;
            continue;
        }

        // Identifiers/keywords
        if (std::isalpha(expr[i]) || expr[i] == '_') {
            int j = i;
            while (j < n && (std::isalnum(expr[j]) || expr[j] == '_')) {
                j++;
            }
            tokens.push_back(expr.substr(i, j - i));
            i = j;
            continue;
        }

        // Parentheses
        if (expr[i] == '(' || expr[i] == ')') {
            tokens.push_back(std::string(1, expr[i]));
            i++;
            continue;
        }

        // Relational operators
        if (i + 1 < n) {
            std::string two = expr.substr(i, 2);
            if (two == "==" || two == "!=" || two == ">=" || two == "<=") {
                tokens.push_back(two);
                i += 2;
                continue;
            }
        }
        if (expr[i] == '>' || expr[i] == '<') {
            tokens.push_back(std::string(1, expr[i]));
            i++;
            continue;
        }

        tokens.push_back(std::string(1, expr[i]));
        i++;
    }
    return tokens;
}

// Shunting-Yard Infix to Postfix (RPN) compiler
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string> ops;

    for (const auto& tok : tokens) {
        std::string upper_tok = to_upper(tok);

        if (tok == "(") {
            ops.push(tok);
        } else if (tok == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("Mismatched parentheses");
            ops.pop(); // discard '('
        } else if (OPS.count(upper_tok)) {
            const auto& o1 = OPS.at(upper_tok);
            while (!ops.empty()) {
                std::string upper_top = to_upper(ops.top());
                if (OPS.count(upper_top)) {
                    const auto& o2 = OPS.at(upper_top);
                    if (o2.precedence > o1.precedence ||
                        (o2.precedence == o1.precedence && !o1.right_assoc)) {
                        output.push_back(ops.top());
                        ops.pop();
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            ops.push(upper_tok); // Push normalized operator
        } else {
            output.push_back(tok);
        }
    }
    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("Mismatched parentheses");
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

// Variant database value types
using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};

// Evaluation Value Helper
struct EvalVal {
    enum Type { DOUBLE, STRING, BOOL } type;
    double d_val = 0.0;
    std::string s_val;
    bool b_val = false;

    explicit EvalVal(double d) : type(DOUBLE), d_val(d) {}
    explicit EvalVal(std::string s) : type(STRING), s_val(s) {}
    explicit EvalVal(bool b) : type(BOOL), b_val(b) {}
};

EvalVal compare_vals(const EvalVal& a, const EvalVal& b, const std::string& op) {
    if (a.type == EvalVal::DOUBLE && b.type == EvalVal::DOUBLE) {
        if (op == "==") return EvalVal(a.d_val == b.d_val);
        if (op == "!=") return EvalVal(a.d_val != b.d_val);
        if (op == ">")  return EvalVal(a.d_val > b.d_val);
        if (op == "<")  return EvalVal(a.d_val < b.d_val);
        if (op == ">=") return EvalVal(a.d_val >= b.d_val);
        if (op == "<=") return EvalVal(a.d_val <= b.d_val);
    } else if (a.type == EvalVal::STRING && b.type == EvalVal::STRING) {
        if (op == "==") return EvalVal(a.s_val == b.s_val);
        if (op == "!=") return EvalVal(a.s_val != b.s_val);
        if (op == ">")  return EvalVal(a.s_val > b.s_val);
        if (op == "<")  return EvalVal(a.s_val < b.s_val);
        if (op == ">=") return EvalVal(a.s_val >= b.s_val);
        if (op == "<=") return EvalVal(a.s_val <= b.s_val);
    } else {
        if (op == "==") return EvalVal(false);
        if (op == "!=") return EvalVal(true);
        return EvalVal(false);
    }
    throw std::runtime_error("Unknown comparison operator: " + op);
}

EvalVal logical_op(const EvalVal& a, const EvalVal& b, const std::string& op) {
    bool a_bool = false;
    if (a.type == EvalVal::BOOL) a_bool = a.b_val;
    else if (a.type == EvalVal::DOUBLE) a_bool = (a.d_val != 0.0);
    else a_bool = !a.s_val.empty();

    bool b_bool = false;
    if (b.type == EvalVal::BOOL) b_bool = b.b_val;
    else if (b.type == EvalVal::DOUBLE) b_bool = (b.d_val != 0.0);
    else b_bool = !b.s_val.empty();

    if (op == "AND") return EvalVal(a_bool && b_bool);
    if (op == "OR")  return EvalVal(a_bool || b_bool);
    throw std::runtime_error("Unknown logical operator: " + op);
}

EvalVal resolve_token(const std::string& tok, const Row& row) {
    // String literal starting and ending with single quotes
    if (tok.size() >= 2 && tok.front() == '\'' && tok.back() == '\'') {
        return EvalVal(tok.substr(1, tok.size() - 2));
    }
    // Number literal
    if (std::isdigit(tok[0]) || tok[0] == '.') {
        try {
            return EvalVal(std::stod(tok));
        } catch (...) {}
    }
    // Row column lookup
    auto it = row.cols.find(tok);
    if (it != row.cols.end()) {
        if (auto* d = std::get_if<double>(&it->second)) {
            return EvalVal(*d);
        }
        if (auto* s = std::get_if<std::string>(&it->second)) {
            return EvalVal(*s);
        }
    }
    throw std::runtime_error("Unknown identifier or literal: " + tok);
}

// Evaluate Postfix / RPN expression
bool eval_rpn(const std::vector<std::string>& rpn, const Row& row) {
    std::stack<EvalVal> stk;
    for (const auto& tok : rpn) {
        std::string upper_tok = to_upper(tok);
        if (OPS.count(upper_tok)) {
            if (stk.size() < 2) throw std::runtime_error("Invalid expression");
            EvalVal b = stk.top(); stk.pop();
            EvalVal a = stk.top(); stk.pop();
            if (upper_tok == "AND" || upper_tok == "OR") {
                stk.push(logical_op(a, b, upper_tok));
            } else {
                stk.push(compare_vals(a, b, upper_tok));
            }
        } else {
            stk.push(resolve_token(tok, row));
        }
    }
    if (stk.empty()) return false;
    if (stk.top().type == EvalVal::BOOL) return stk.top().b_val;
    if (stk.top().type == EvalVal::DOUBLE) return stk.top().d_val != 0.0;
    return !stk.top().s_val.empty();
}

struct SelectQuery {
    std::vector<std::string> columns; // Empty means SELECT *
    std::string tableName;
    std::string whereClause;
};

SelectQuery parse_select(const std::string& sql) {
    auto tokens = tokenize(sql);
    SelectQuery q;
    int i = 0, n = tokens.size();
    
    if (i < n && to_upper(tokens[i]) == "SELECT") {
        i++;
    } else {
        throw std::runtime_error("Query must start with SELECT");
    }

    // Read projected columns
    while (i < n && to_upper(tokens[i]) != "FROM") {
        if (tokens[i] == ",") {
            i++;
            continue;
        }
        if (tokens[i] == "*") {
            q.columns.clear();
        } else {
            q.columns.push_back(tokens[i]);
        }
        i++;
    }

    if (i < n && to_upper(tokens[i]) == "FROM") {
        i++;
    } else {
        throw std::runtime_error("Expected FROM keyword");
    }

    if (i < n) {
        q.tableName = tokens[i];
        i++;
    } else {
        throw std::runtime_error("Expected table name");
    }

    if (i < n && to_upper(tokens[i]) == "WHERE") {
        i++;
        std::string where_str;
        while (i < n) {
            where_str += (where_str.empty() ? "" : " ") + tokens[i];
            i++;
        }
        q.whereClause = where_str;
    }
    return q;
}

std::vector<std::string> get_where_tokens(const std::string& sql) {
    auto tokens = tokenize(sql);
    int i = 0, n = tokens.size();
    while (i < n && to_upper(tokens[i]) != "WHERE") {
        i++;
    }
    std::vector<std::string> where_toks;
    if (i < n && to_upper(tokens[i]) == "WHERE") {
        i++;
        while (i < n) {
            where_toks.push_back(tokens[i]);
            i++;
        }
    }
    return where_toks;
}

void run_query(const std::string& sql, const std::vector<Row>& table) {
    std::cout << "SQL Query: " << sql << "\n";
    try {
        SelectQuery q = parse_select(sql);
        auto where_toks = get_where_tokens(sql);
        std::vector<std::string> rpn;
        if (!where_toks.empty()) {
            rpn = to_rpn(where_toks);
        }

        // Print header
        if (q.columns.empty()) {
            std::cout << "| id | name | age | city |\n";
            std::cout << "|---|---|---|---|\n";
        } else {
            std::cout << "|";
            for (const auto& col : q.columns) {
                std::cout << " " << col << " |";
            }
            std::cout << "\n|";
            for (size_t k = 0; k < q.columns.size(); k++) {
                std::cout << "---|";
            }
            std::cout << "\n";
        }

        // Scan, filter and project
        int match_count = 0;
        for (const auto& row : table) {
            bool matches = true;
            if (!rpn.empty()) {
                matches = eval_rpn(rpn, row);
            }
            if (matches) {
                match_count++;
                if (q.columns.empty()) {
                    double id = 0;
                    std::string name;
                    double age = 0;
                    std::string city;
                    
                    if (row.cols.count("id")) id = std::get<double>(row.cols.at("id"));
                    if (row.cols.count("name")) name = std::get<std::string>(row.cols.at("name"));
                    if (row.cols.count("age")) age = std::get<double>(row.cols.at("age"));
                    if (row.cols.count("city")) city = std::get<std::string>(row.cols.at("city"));

                    std::cout << "| " << id << " | " << name << " | " << age << " | " << city << " |\n";
                } else {
                    std::cout << "|";
                    for (const auto& col : q.columns) {
                        auto it = row.cols.find(col);
                        if (it != row.cols.end()) {
                            if (auto* d = std::get_if<double>(&it->second)) {
                                std::cout << " " << *d << " |";
                            } else if (auto* s = std::get_if<std::string>(&it->second)) {
                                std::cout << " " << *s << " |";
                            }
                        } else {
                            std::cout << " NULL |";
                        }
                    }
                    std::cout << "\n";
                }
            }
        }
        std::cout << "(" << match_count << " rows returned)\n\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n\n";
    }
}

int main() {
    // Populate the database table `users`
    std::vector<Row> users = {
        {{{ "id", 1.0 }, { "name", std::string("Amit") },     { "age", 35.0 }, { "city", std::string("Delhi") }}},
        {{{ "id", 2.0 }, { "name", std::string("Mehul") },    { "age", 25.0 }, { "city", std::string("Mumbai") }}},
        {{{ "id", 3.0 }, { "name", std::string("Rahul") },    { "age", 40.0 }, { "city", std::string("Mumbai") }}},
        {{{ "id", 4.0 }, { "name", std::string("Sneha") },    { "age", 32.0 }, { "city", std::string("Bangalore") }}},
        {{{ "id", 5.0 }, { "name", std::string("Karan") },    { "age", 45.0 }, { "city", std::string("Mumbai") }}}
    };

    // SQL queries to evaluate
    std::vector<std::string> queries = {
        "SELECT id, name, city FROM users WHERE city == 'Mumbai'",
        "SELECT id, name, age FROM users WHERE age > 30",
        "SELECT id, name, age, city FROM users WHERE city == 'Mumbai' AND age > 30"
    };

    for (const auto& sql : queries) {
        run_query(sql, users);
    }

    return 0;
}
