#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <cctype>
#include <cmath>

using namespace std;

// =====================================================
// PART 1: SHUNTING-YARD ALGORITHM
// =====================================================

struct OpInfo {
    int precedence;
    bool right_assoc;
};

const unordered_map<string, OpInfo> OPS = {
    {"||", {1, false}},
    {"&&", {2, false}},
    {"=",  {3, false}},
    {"!=", {3, false}},
    {"<",  {4, false}},
    {">",  {4, false}},
    {"<=", {4, false}},
    {">=", {4, false}},
    {"+",  {5, false}},
    {"-",  {5, false}},
    {"*",  {6, false}},
    {"/",  {6, false}},
    {"^",  {7, true}}
};

// -----------------------------------------------------
// Tokenizer
// -----------------------------------------------------

vector<string> tokenize(const string& expr) {
    vector<string> tokens;

    int i = 0;
    int n = expr.size();

    while (i < n) {

        if (isspace(expr[i])) {
            i++;
            continue;
        }

        if (isdigit(expr[i]) ||
            (expr[i] == '.' &&
             i + 1 < n &&
             isdigit(expr[i + 1]))) {

            int j = i;

            while (j < n &&
                   (isdigit(expr[j]) || expr[j] == '.'))
                j++;

            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }

        else if (isalpha(expr[i]) || expr[i] == '_') {

            int j = i;

            while (j < n &&
                   (isalnum(expr[j]) || expr[j] == '_'))
                j++;

            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }

        else if (expr[i] == '(' || expr[i] == ')') {
            tokens.push_back(string(1, expr[i]));
            i++;
        }

        else {

            if (i + 1 < n) {
                string two = expr.substr(i, 2);

                if (OPS.count(two)) {
                    tokens.push_back(two);
                    i += 2;
                    continue;
                }
            }

            tokens.push_back(string(1, expr[i]));
            i++;
        }
    }

    return tokens;
}

// -----------------------------------------------------
// Infix -> RPN
// -----------------------------------------------------

vector<string> to_rpn(const vector<string>& tokens) {

    vector<string> output;
    stack<string> ops;

    for (const auto& tok : tokens) {

        if (tok == "(") {
            ops.push(tok);
        }

        else if (tok == ")") {

            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }

            if (ops.empty())
                throw runtime_error("Mismatched parentheses");

            ops.pop();
        }

        else if (OPS.count(tok)) {

            const auto& o1 = OPS.at(tok);

            while (!ops.empty() && OPS.count(ops.top())) {

                const auto& o2 = OPS.at(ops.top());

                if (o2.precedence > o1.precedence ||
                    (o2.precedence == o1.precedence &&
                     !o1.right_assoc)) {

                    output.push_back(ops.top());
                    ops.pop();
                }
                else {
                    break;
                }
            }

            ops.push(tok);
        }

        else {
            output.push_back(tok);
        }
    }

    while (!ops.empty()) {

        if (ops.top() == "(")
            throw runtime_error("Mismatched parentheses");

        output.push_back(ops.top());
        ops.pop();
    }

    return output;
}

// -----------------------------------------------------
// Evaluate RPN
// -----------------------------------------------------

double eval_rpn(
    const vector<string>& rpn,
    const unordered_map<string, double>& vars) {

    stack<double> stk;

    for (const auto& tok : rpn) {

        if (OPS.count(tok)) {

            double b = stk.top();
            stk.pop();

            double a = stk.top();
            stk.pop();

            if (tok == "+")
                stk.push(a + b);

            else if (tok == "-")
                stk.push(a - b);

            else if (tok == "*")
                stk.push(a * b);

            else if (tok == "/")
                stk.push(a / b);

            else if (tok == "^")
                stk.push(pow(a, b));

            else if (tok == "<")
                stk.push(a < b ? 1.0 : 0.0);

            else if (tok == ">")
                stk.push(a > b ? 1.0 : 0.0);

            else if (tok == "<=")
                stk.push(a <= b ? 1.0 : 0.0);

            else if (tok == ">=")
                stk.push(a >= b ? 1.0 : 0.0);

            else if (tok == "=")
                stk.push(a == b ? 1.0 : 0.0);

            else if (tok == "!=")
                stk.push(a != b ? 1.0 : 0.0);

            else if (tok == "&&")
                stk.push((a && b) ? 1.0 : 0.0);

            else if (tok == "||")
                stk.push((a || b) ? 1.0 : 0.0);
        }

        else {

            try {
                stk.push(stod(tok));
            }

            catch (...) {

                auto it = vars.find(tok);

                if (it == vars.end())
                    throw runtime_error(
                        "Unknown variable: " + tok);

                stk.push(it->second);
            }
        }
    }

    return stk.top();
}

// -----------------------------------------------------
// Demo
// -----------------------------------------------------

void shunting_demo() {

    string expr =
        "age * 2 + salary / 1000 > 100";

    auto tokens = tokenize(expr);
    auto rpn = to_rpn(tokens);

    cout << "Expression : "
         << expr << "\n";

    cout << "RPN        : ";

    for (auto& t : rpn)
        cout << t << " ";

    cout << "\n";

    unordered_map<string, double> vars = {
        {"age", 30},
        {"salary", 50000}
    };

    double result = eval_rpn(rpn, vars);

    cout << "Result     : "
         << (result ? "true" : "false")
         << "\n\n";
}

// =====================================================
// PART 2: SQL PARSER
// =====================================================

using Value = variant<double, string>;

struct Row {
    unordered_map<string, Value> cols;
};

double row_val(
    const Row& row,
    const string& col) {

    auto it = row.cols.find(col);

    if (it == row.cols.end())
        return 0.0;

    if (auto* d = get_if<double>(&it->second))
        return *d;

    if (auto* s = get_if<string>(&it->second)) {
        try {
            return stod(*s);
        }
        catch (...) {
        }
    }

    return 0.0;
}

struct SelectQuery {

    vector<string> columns;

    string from;

    string where_raw;

    string order_by;

    bool order_asc = true;

    int limit = -1;
};

string to_upper(string s) {

    for (char& c : s)
        c = toupper(c);

    return s;
}

// -----------------------------------------------------
// Parser
// -----------------------------------------------------

SelectQuery parse_select(const string& sql) {

    SelectQuery q;

    istringstream ss(sql);

    string word;

    ss >> word;

    while (ss >> word &&
           to_upper(word) != "FROM") {

        if (!word.empty() &&
            word.back() == ',')
            word.pop_back();

        if (word != "*")
            q.columns.push_back(word);
    }

    ss >> q.from;

    string remaining;
    getline(ss, remaining);

    size_t wherePos =
        to_upper(remaining).find("WHERE");

    size_t orderPos =
        to_upper(remaining).find("ORDER BY");

    size_t limitPos =
        to_upper(remaining).find("LIMIT");

    if (wherePos != string::npos) {

        size_t endPos = remaining.size();

        if (orderPos != string::npos)
            endPos = min(endPos, orderPos);

        if (limitPos != string::npos)
            endPos = min(endPos, limitPos);

        q.where_raw =
            remaining.substr(
                wherePos + 5,
                endPos - wherePos - 5);
    }

    if (orderPos != string::npos) {

        istringstream ord(
            remaining.substr(orderPos + 8));

        ord >> q.order_by;

        string dir;

        if (ord >> dir) {
            if (to_upper(dir) == "DESC")
                q.order_asc = false;
        }
    }

    if (limitPos != string::npos) {

        istringstream lim(
            remaining.substr(limitPos + 5));

        lim >> q.limit;
    }

    return q;
}

// -----------------------------------------------------
// Executor
// -----------------------------------------------------

vector<Row> execute(
    const SelectQuery& q,
    const vector<Row>& data) {

    vector<string> rpn;

    if (!q.where_raw.empty())
        rpn = to_rpn(tokenize(q.where_raw));

    vector<Row> result;

    for (const auto& row : data) {

        if (!rpn.empty()) {

            unordered_map<string, double> vars;

            for (const auto& [k, v] : row.cols)
                vars[k] = row_val(row, k);

            if (!eval_rpn(rpn, vars))
                continue;
        }

        if (q.columns.empty()) {

            result.push_back(row);
        }

        else {

            Row projected;

            for (const auto& col : q.columns) {

                if (row.cols.count(col))
                    projected.cols[col]
                        = row.cols.at(col);
            }

            result.push_back(projected);
        }
    }

    if (!q.order_by.empty()) {

        sort(
            result.begin(),
            result.end(),
            [&](const Row& a,
                const Row& b) {

                double va =
                    row_val(a, q.order_by);

                double vb =
                    row_val(b, q.order_by);

                return q.order_asc
                    ? va < vb
                    : va > vb;
            });
    }

    if (q.limit >= 0 &&
        result.size() >
        static_cast<size_t>(q.limit))
        result.resize(q.limit);

    return result;
}

// -----------------------------------------------------
// Print Rows
// -----------------------------------------------------

void print_rows(
    const vector<Row>& rows) {

    for (const auto& row : rows) {

        for (const auto& [k, v] : row.cols) {

            cout << k << "=";

            if (auto* d =
                get_if<double>(&v))
                cout << *d;

            if (auto* s =
                get_if<string>(&v))
                cout << *s;

            cout << "  ";
        }

        cout << "\n";
    }
}

// =====================================================
// MAIN
// =====================================================

int main() {

    shunting_demo();

    vector<Row> students = {
        { { {"id",1.0}, {"name",string("Alice")}, {"age",22.0}, {"gpa",3.8} } },
        { { {"id",2.0}, {"name",string("Bob")}, {"age",25.0}, {"gpa",2.9} } },
        { { {"id",3.0}, {"name",string("Carol")}, {"age",21.0}, {"gpa",3.5} } },
        { { {"id",4.0}, {"name",string("Dave")}, {"age",30.0}, {"gpa",3.1} } }
    };

    vector<string> queries = {

        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",

        "SELECT * FROM students WHERE age >= 22 && age <= 26"
    };

    for (const auto& sql : queries) {

        cout << "SQL: "
             << sql << "\n";

        auto q = parse_select(sql);

        auto result =
            execute(q, students);

        print_rows(result);

        cout << "\n";
    }

    return 0;
}