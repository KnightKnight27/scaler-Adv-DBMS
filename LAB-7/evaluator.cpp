#include "evaluator.h"
#include "lexer.h"
#include "parser.h"
#include <stack>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <iostream>

double Evaluator::getNumeric(const Value& val) {
    if (val.type == Value::DOUBLE) {
        return val.d_val;
    }
    try {
        return std::stod(val.s_val);
    } catch (...) {
        return 0.0;
    }
}

std::string Evaluator::getString(const Value& val) {
    if (val.type == Value::STRING) {
        return val.s_val;
    }
    double d = val.d_val;
    std::string s = std::to_string(d);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

static bool compareValues(const std::string& op, const Value& lhs, const Value& rhs) {
    if (lhs.type == Value::DOUBLE && rhs.type == Value::DOUBLE) {
        double a = lhs.d_val;
        double b = rhs.d_val;
        if (op == "=") return a == b;
        if (op == "!=" || op == "<>") return a != b;
        if (op == "<") return a < b;
        if (op == ">") return a > b;
        if (op == "<=") return a <= b;
        if (op == ">=") return a >= b;
    }
    else if (lhs.type == Value::STRING && rhs.type == Value::STRING) {
        const std::string& a = lhs.s_val;
        const std::string& b = rhs.s_val;
        if (op == "=") return a == b;
        if (op == "!=" || op == "<>") return a != b;
        if (op == "<") return a < b;
        if (op == ">") return a > b;
        if (op == "<=") return a <= b;
        if (op == ">=") return a >= b;
    }
    else {
        double a, b;
        bool a_ok = false, b_ok = false;
        if (lhs.type == Value::DOUBLE) {
            a = lhs.d_val;
            a_ok = true;
        } else {
            try {
                a = std::stod(lhs.s_val);
                a_ok = true;
            } catch (...) {}
        }
        if (rhs.type == Value::DOUBLE) {
            b = rhs.d_val;
            b_ok = true;
        } else {
            try {
                b = std::stod(rhs.s_val);
                b_ok = true;
            } catch (...) {}
        }
        if (a_ok && b_ok) {
            if (op == "=") return a == b;
            if (op == "!=" || op == "<>") return a != b;
            if (op == "<") return a < b;
            if (op == ">") return a > b;
            if (op == "<=") return a <= b;
            if (op == ">=") return a >= b;
        } else {
            std::string sa = lhs.type == Value::DOUBLE ? std::to_string(lhs.d_val) : lhs.s_val;
            std::string sb = rhs.type == Value::DOUBLE ? std::to_string(rhs.d_val) : rhs.s_val;
            if (op == "=") return sa == sb;
            if (op == "!=" || op == "<>") return sa != sb;
            if (op == "<") return sa < sb;
            if (op == ">") return sa > sb;
            if (op == "<=") return sa <= sb;
            if (op == ">=") return sa >= sb;
        }
    }
    throw std::runtime_error("Invalid comparison operator: " + op);
}

bool Evaluator::evalRPN(const std::vector<RpnToken>& rpn, const Row& row) {
    std::stack<Value> stk;

    for (const auto& tok : rpn) {
        if (tok.type == RpnTokenType::LITERAL) {
            stk.push(tok.val);
        } else if (tok.type == RpnTokenType::COLUMN_REF) {
            std::string colName = tok.val.s_val;
            auto it = row.cols.find(colName);
            if (it != row.cols.end()) {
                stk.push(it->second);
            } else {
                throw std::runtime_error("Column not found in row: " + colName);
            }
        } else if (tok.type == RpnTokenType::OPERATOR) {
            std::string op = tok.val.s_val;
            if (op == "NOT" || op == "!") {
                if (stk.empty()) throw std::runtime_error("Empty stack for unary NOT");
                Value a = stk.top(); stk.pop();
                double a_val = getNumeric(a);
                stk.push(Value((a_val == 0.0) ? 1.0 : 0.0));
            } else {
                if (stk.size() < 2) throw std::runtime_error("Stack underflow for binary operator: " + op);
                Value b = stk.top(); stk.pop();
                Value a = stk.top(); stk.pop();

                if (op == "AND" || op == "&&") {
                    double da = getNumeric(a);
                    double db = getNumeric(b);
                    stk.push(Value((da != 0.0 && db != 0.0) ? 1.0 : 0.0));
                } else if (op == "OR" || op == "||") {
                    double da = getNumeric(a);
                    double db = getNumeric(b);
                    stk.push(Value((da != 0.0 || db != 0.0) ? 1.0 : 0.0));
                } else if (op == "=" || op == "!=" || op == "<>" || op == "<" || op == ">" || op == "<=" || op == ">=") {
                    bool res = compareValues(op, a, b);
                    stk.push(Value(res ? 1.0 : 0.0));
                } else if (op == "+" || op == "-" || op == "*" || op == "/" || op == "^") {
                    double da = getNumeric(a);
                    double db = getNumeric(b);
                    double res = 0.0;
                    if (op == "+") res = da + db;
                    else if (op == "-") res = da - db;
                    else if (op == "*") res = da * db;
                    else if (op == "/") {
                        if (db == 0.0) throw std::runtime_error("Division by zero");
                        res = da / db;
                    }
                    else if (op == "^") res = std::pow(da, db);
                    stk.push(Value(res));
                } else {
                    throw std::runtime_error("Unsupported operator: " + op);
                }
            }
        }
    }

    if (stk.empty()) throw std::runtime_error("Empty evaluation stack at the end");
    return getNumeric(stk.top()) != 0.0;
}

std::vector<Row> Evaluator::execute(const SelectQuery& q, const std::vector<Row>& data) {
    std::vector<RpnToken> rpn;
    if (!q.where_raw.empty()) {
        Lexer lexer(q.where_raw);
        auto tokens = lexer.tokenize();
        rpn = SQLParser::toRPN(tokens);
    }

    std::vector<Row> result;

    for (const auto& row : data) {
        if (!rpn.empty()) {
            if (!evalRPN(rpn, row)) {
                continue;
            }
        }

        if (q.columns.empty()) {
            result.push_back(row);
        } else {
            Row projected;
            for (const auto& col : q.columns) {
                auto it = row.cols.find(col);
                if (it != row.cols.end()) {
                    projected.cols[col] = it->second;
                }
            }
            result.push_back(projected);
        }
    }

    if (!q.order_by.empty()) {
        std::stable_sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            auto itA = a.cols.find(q.order_by);
            auto itB = b.cols.find(q.order_by);
            Value valA = (itA != a.cols.end()) ? itA->second : Value(0.0);
            Value valB = (itB != b.cols.end()) ? itB->second : Value(0.0);

            const Value& first = q.order_asc ? valA : valB;
            const Value& second = q.order_asc ? valB : valA;

            if (first.type == Value::DOUBLE && second.type == Value::DOUBLE) {
                return first.d_val < second.d_val;
            } else if (first.type == Value::STRING && second.type == Value::STRING) {
                return first.s_val < second.s_val;
            } else {
                return getNumeric(first) < getNumeric(second);
            }
        });
    }

    if (q.limit >= 0 && static_cast<int>(result.size()) > q.limit) {
        result.resize(q.limit);
    }

    return result;
}
