
#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>
#include <algorithm>
#include <cmath>
#include <cctype>

using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> cols;
};

struct OpInfo {
    int precedence;
    bool right_assoc;
};

const std::unordered_map<std::string, OpInfo> OPS = {
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

std::vector<std::string> tokenize(const std::string& expr) {

    std::vector<std::string> tokens;

    int i = 0;
    int n = expr.size();

    while (i < n) {

        if (std::isspace(expr[i])) {
            i++;
            continue;
        }

        if (std::isdigit(expr[i]) ||
            (expr[i] == '.' &&
             i + 1 < n &&
             std::isdigit(expr[i + 1]))) {

            int j = i;

            while (j < n &&
                   (std::isdigit(expr[j]) ||
                    expr[j] == '.'))
                j++;

            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }

        else if (std::isalpha(expr[i]) ||
                 expr[i] == '_') {

            int j = i;

            while (j < n &&
                  (std::isalnum(expr[j]) ||
                   expr[j] == '_'))
                j++;

            tokens.push_back(expr.substr(i, j - i));
            i = j;
        }

        else if (expr[i] == '(' ||
                 expr[i] == ')') {

            tokens.push_back(std::string(1, expr[i]));
            i++;
        }

        else {

            if (i + 1 < n) {

                std::string two =
                    expr.substr(i, 2);

                if (OPS.count(two)) {
                    tokens.push_back(two);
                    i += 2;
                    continue;
                }
            }

            tokens.push_back(
                std::string(1, expr[i]));

            i++;
        }
    }

    return tokens;
}

std::vector<std::string> to_rpn(
    const std::vector<std::string>& tokens) {

    std::vector<std::string> output;
    std::stack<std::string> operators;

    for (auto token : tokens) {

        if (token == "(") {
            operators.push(token);
        }

        else if (token == ")") {

            while (!operators.empty() &&
                   operators.top() != "(") {

                output.push_back(
                    operators.top());

                operators.pop();
            }

            operators.pop();
        }

        else if (OPS.count(token)) {

            auto current = OPS.at(token);

            while (!operators.empty() &&
                   OPS.count(operators.top())) {

                auto top =
                    OPS.at(operators.top());

                if (top.precedence >
                        current.precedence ||

                   (top.precedence ==
                        current.precedence &&
                    !current.right_assoc)) {

                    output.push_back(
                        operators.top());

                    operators.pop();
                }

                else {
                    break;
                }
            }

            operators.push(token);
        }

        else {
            output.push_back(token);
        }
    }

    while (!operators.empty()) {

        output.push_back(
            operators.top());

        operators.pop();
    }

    return output;
}

double eval_rpn(
    const std::vector<std::string>& rpn,
    const std::unordered_map<
        std::string,double>& vars) {

    std::stack<double> st;

    for (auto token : rpn) {

        if (OPS.count(token)) {

            double b = st.top();
            st.pop();

            double a = st.top();
            st.pop();

            if (token == "+") st.push(a + b);
            else if (token == "-") st.push(a - b);
            else if (token == "*") st.push(a * b);
            else if (token == "/") st.push(a / b);
            else if (token == "^") st.push(pow(a,b));

            else if (token == "<")
                st.push(a < b);

            else if (token == ">")
                st.push(a > b);

            else if (token == "<=")
                st.push(a <= b);

            else if (token == ">=")
                st.push(a >= b);

            else if (token == "=")
                st.push(a == b);

            else if (token == "!=")
                st.push(a != b);

            else if (token == "&&")
                st.push(a && b);

            else if (token == "||")
                st.push(a || b);
        }

        else {

            try {
                st.push(std::stod(token));
            }

            catch (...) {

                auto it =
                    vars.find(token);

                if (it == vars.end())
                    throw std::runtime_error(
                        "Unknown variable");

                st.push(it->second);
            }
        }
    }

    return st.top();
}

double row_val(
    const Row& row,
    const std::string& col) {

    auto it = row.cols.find(col);

    if (it == row.cols.end())
        return 0.0;

    if (auto* d =
        std::get_if<double>(&it->second))
        return *d;

    if (auto* s =
        std::get_if<std::string>(&it->second)) {

        try {
            return std::stod(*s);
        }
        catch (...) {}
    }

    return 0.0;
}

struct SelectQuery {

    std::vector<std::string> columns;

    std::string from;

    std::string where_raw;

    std::string order_by;

    bool order_asc = true;

    int limit = -1;
};

std::string to_upper(std::string s) {

    for (auto& c : s)
        c = std::toupper(c);

    return s;
}

SelectQuery parse_select(
    const std::string& sql) {

    SelectQuery q;

    std::istringstream ss(sql);

    std::string word;

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

    while (ss >> word) {

        std::string kw =
            to_upper(word);

        if (kw == "WHERE") {

            std::string clause;
            std::string temp;

            while (ss >> temp) {

                if (to_upper(temp) == "ORDER" ||
                    to_upper(temp) == "LIMIT") {

                    word = temp;
                    break;
                }

                if (!clause.empty())
                    clause += " ";

                clause += temp;
            }

            q.where_raw = clause;
            kw = to_upper(word);
        }

        if (kw == "ORDER") {

            ss >> word;
            ss >> q.order_by;

            std::string dir;

            if (ss >> dir) {

                if (to_upper(dir)
                    == "DESC")
                    q.order_asc = false;
            }
        }

        if (kw == "LIMIT") {
            ss >> q.limit;
        }
    }

    return q;
}

std::vector<Row> execute(
    const SelectQuery& q,
    const std::vector<Row>& data) {

    std::vector<Row> result;

    std::vector<std::string> rpn;

    if (!q.where_raw.empty())
        rpn = to_rpn(
            tokenize(q.where_raw));

    for (auto& row : data) {

        if (!rpn.empty()) {

            std::unordered_map<
                std::string,double> vars;

            for (auto& [k,v] : row.cols)
                vars[k] =
                    row_val(row,k);

            if (!eval_rpn(rpn, vars))
                continue;
        }

        if (q.columns.empty()) {

            result.push_back(row);
        }

        else {

            Row projected;

            for (auto& col : q.columns) {

                if (row.cols.count(col))
                    projected.cols[col] =
                        row.cols.at(col);
            }

            result.push_back(projected);
        }
    }

    if (!q.order_by.empty()) {

        std::sort(
            result.begin(),
            result.end(),

            [&](const Row& a,
                const Row& b) {

                double va =
                    row_val(a,q.order_by);

                double vb =
                    row_val(b,q.order_by);

                return q.order_asc ?
                    va < vb :
                    va > vb;
            });
    }

    if (q.limit >= 0 &&
        result.size() > q.limit) {

        result.resize(q.limit);
    }

    return result;
}

void print_rows(
    const std::vector<Row>& rows) {

    for (auto& row : rows) {

        for (auto& [k,v] : row.cols) {

            std::cout << k << "=";

            if (auto* d =
                std::get_if<double>(&v))
                std::cout << *d;

            if (auto* s =
                std::get_if<std::string>(&v))
                std::cout << *s;

            std::cout << "  ";
        }

        std::cout << "\n";
    }
}

int main() {

    std::vector<Row> students = {

        {{{"id",1.0},
          {"name",std::string("Alice")},
          {"age",22.0},
          {"gpa",3.8}}},

        {{{"id",2.0},
          {"name",std::string("Bob")},
          {"age",25.0},
          {"gpa",2.9}}},

        {{{"id",3.0},
          {"name",std::string("Carol")},
          {"age",21.0},
          {"gpa",3.5}}},

        {{{"id",4.0},
          {"name",std::string("Dave")},
          {"age",30.0},
          {"gpa",3.1}}}
    };

    std::vector<std::string> queries = {

        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",

        "SELECT * FROM students WHERE age >= 22 && age <= 26"
    };

    for (auto& sql : queries) {

        std::cout
            << "\nSQL Query:\n"
            << sql
            << "\n\n";

        auto query =
            parse_select(sql);

        auto result =
            execute(query,
                    students);

        print_rows(result);

        std::cout
            << "\n-------------------\n";
    }

    return 0;
}

