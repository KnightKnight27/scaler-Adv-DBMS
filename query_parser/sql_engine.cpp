#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct OperatorInfo {
    int precedence;
    bool right_associative;
};

const std::unordered_map<std::string, OperatorInfo> kOperators = {
    {"||", {1, false}},
    {"&&", {2, false}},
    {"=", {3, false}},
    {"!=", {3, false}},
    {"<", {4, false}},
    {">", {4, false}},
    {"<=", {4, false}},
    {">=", {4, false}},
    {"+", {5, false}},
    {"-", {5, false}},
    {"*", {6, false}},
    {"/", {6, false}},
    {"^", {7, true}},
};

std::string upper_copy(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

std::vector<std::string> lex(const std::string& input) {
    std::vector<std::string> out;

    for (std::size_t i = 0; i < input.size();) {
        const unsigned char ch = static_cast<unsigned char>(input[i]);
        if (std::isspace(ch)) {
            ++i;
            continue;
        }

        if (std::isalpha(ch) || input[i] == '_') {
            std::size_t start = i++;
            while (i < input.size()) {
                const unsigned char next = static_cast<unsigned char>(input[i]);
                if (!std::isalnum(next) && input[i] != '_') {
                    break;
                }
                ++i;
            }
            std::string word = input.substr(start, i - start);
            const std::string upper = upper_copy(word);
            if (upper == "AND") {
                out.push_back("&&");
            } else if (upper == "OR") {
                out.push_back("||");
            } else {
                out.push_back(word);
            }
            continue;
        }

        if (std::isdigit(ch) || input[i] == '.') {
            std::size_t start = i++;
            while (i < input.size()) {
                const unsigned char next = static_cast<unsigned char>(input[i]);
                if (!std::isdigit(next) && input[i] != '.') {
                    break;
                }
                ++i;
            }
            out.push_back(input.substr(start, i - start));
            continue;
        }

        if (i + 1 < input.size()) {
            std::string two = input.substr(i, 2);
            if (kOperators.count(two)) {
                out.push_back(two);
                i += 2;
                continue;
            }
        }

        out.push_back(std::string(1, input[i]));
        ++i;
    }

    return out;
}

std::vector<std::string> to_postfix(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string> ops;

    for (const std::string& token : tokens) {
        if (token == "(") {
            ops.push(token);
        } else if (token == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) {
                throw std::runtime_error("mismatched parenthesis");
            }
            ops.pop();
        } else if (kOperators.count(token)) {
            const OperatorInfo current = kOperators.at(token);
            while (!ops.empty() && kOperators.count(ops.top())) {
                const OperatorInfo previous = kOperators.at(ops.top());
                const bool should_pop =
                    previous.precedence > current.precedence ||
                    (previous.precedence == current.precedence && !current.right_associative);
                if (!should_pop) {
                    break;
                }
                output.push_back(ops.top());
                ops.pop();
            }
            ops.push(token);
        } else if (token != ",") {
            output.push_back(token);
        }
    }

    while (!ops.empty()) {
        if (ops.top() == "(") {
            throw std::runtime_error("mismatched parenthesis");
        }
        output.push_back(ops.top());
        ops.pop();
    }

    return output;
}

using Value = std::variant<double, std::string>;

struct Row {
    std::unordered_map<std::string, Value> fields;
};

double as_number(const Row& row, const std::string& name) {
    auto it = row.fields.find(name);
    if (it == row.fields.end()) {
        throw std::runtime_error("unknown column: " + name);
    }
    if (const double* number = std::get_if<double>(&it->second)) {
        return *number;
    }
    if (const std::string* text = std::get_if<std::string>(&it->second)) {
        return std::stod(*text);
    }
    throw std::runtime_error("unsupported column value");
}

double eval_postfix(const std::vector<std::string>& postfix, const Row& row) {
    std::stack<double> values;

    for (const std::string& token : postfix) {
        if (!kOperators.count(token)) {
            char* end = nullptr;
            const double parsed = std::strtod(token.c_str(), &end);
            if (end != token.c_str() && *end == '\0') {
                values.push(parsed);
            } else {
                values.push(as_number(row, token));
            }
            continue;
        }

        if (values.size() < 2) {
            throw std::runtime_error("not enough operands for " + token);
        }
        const double rhs = values.top();
        values.pop();
        const double lhs = values.top();
        values.pop();

        if (token == "+") values.push(lhs + rhs);
        else if (token == "-") values.push(lhs - rhs);
        else if (token == "*") values.push(lhs * rhs);
        else if (token == "/") values.push(lhs / rhs);
        else if (token == "^") values.push(std::pow(lhs, rhs));
        else if (token == "<") values.push(lhs < rhs);
        else if (token == ">") values.push(lhs > rhs);
        else if (token == "<=") values.push(lhs <= rhs);
        else if (token == ">=") values.push(lhs >= rhs);
        else if (token == "=") values.push(lhs == rhs);
        else if (token == "!=") values.push(lhs != rhs);
        else if (token == "&&") values.push(lhs != 0.0 && rhs != 0.0);
        else if (token == "||") values.push(lhs != 0.0 || rhs != 0.0);
    }

    if (values.size() != 1) {
        throw std::runtime_error("invalid expression");
    }
    return values.top();
}

struct SelectQuery {
    std::vector<std::string> columns;
    std::string table;
    std::vector<std::string> where_tokens;
    std::string order_column;
    bool ascending = true;
    int limit = -1;
};

bool keyword(const std::string& token, const std::string& expected) {
    return upper_copy(token) == expected;
}

SelectQuery parse_select(const std::string& sql) {
    const std::vector<std::string> tokens = lex(sql);
    std::size_t pos = 0;

    auto take = [&]() -> std::string {
        if (pos >= tokens.size()) {
            throw std::runtime_error("unexpected end of SQL");
        }
        return tokens[pos++];
    };

    if (!keyword(take(), "SELECT")) {
        throw std::runtime_error("query must start with SELECT");
    }

    SelectQuery query;
    while (pos < tokens.size() && !keyword(tokens[pos], "FROM")) {
        std::string token = take();
        if (token == ",") {
            continue;
        }
        if (token != "*") {
            query.columns.push_back(token);
        }
    }

    if (pos >= tokens.size() || !keyword(take(), "FROM")) {
        throw std::runtime_error("missing FROM");
    }
    query.table = take();

    while (pos < tokens.size()) {
        std::string token = take();
        if (keyword(token, "WHERE")) {
            while (pos < tokens.size() && !keyword(tokens[pos], "ORDER") && !keyword(tokens[pos], "LIMIT")) {
                query.where_tokens.push_back(take());
            }
        } else if (keyword(token, "ORDER")) {
            if (pos >= tokens.size() || !keyword(take(), "BY")) {
                throw std::runtime_error("expected ORDER BY");
            }
            query.order_column = take();
            if (pos < tokens.size() && (keyword(tokens[pos], "ASC") || keyword(tokens[pos], "DESC"))) {
                query.ascending = keyword(take(), "ASC");
            }
        } else if (keyword(token, "LIMIT")) {
            query.limit = std::stoi(take());
        }
    }

    return query;
}

std::vector<Row> execute(const SelectQuery& query, const std::vector<Row>& table) {
    const std::vector<std::string> filter =
        query.where_tokens.empty() ? std::vector<std::string>{} : to_postfix(query.where_tokens);

    std::vector<Row> rows;
    for (const Row& row : table) {
        if (!filter.empty() && eval_postfix(filter, row) == 0.0) {
            continue;
        }

        if (query.columns.empty()) {
            rows.push_back(row);
            continue;
        }

        Row projected;
        for (const std::string& column : query.columns) {
            projected.fields[column] = row.fields.at(column);
        }
        rows.push_back(projected);
    }

    if (!query.order_column.empty()) {
        std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            const double lhs = as_number(a, query.order_column);
            const double rhs = as_number(b, query.order_column);
            return query.ascending ? lhs < rhs : lhs > rhs;
        });
    }

    if (query.limit >= 0 && static_cast<int>(rows.size()) > query.limit) {
        rows.resize(query.limit);
    }

    return rows;
}

void print_value(const Value& value) {
    if (const double* number = std::get_if<double>(&value)) {
        std::cout << *number;
    } else {
        std::cout << std::get<std::string>(value);
    }
}

void print_rows(const std::vector<Row>& rows, const std::vector<std::string>& columns) {
    for (const Row& row : rows) {
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i) {
                std::cout << " | ";
            }
            std::cout << columns[i] << '=';
            print_value(row.fields.at(columns[i]));
        }
        std::cout << '\n';
    }
}

int main() {
    const std::string expression = "(age + bonus) * 2 >= salary / 1000";
    const std::vector<std::string> postfix = to_postfix(lex(expression));
    Row employee{{{"age", 31.0}, {"bonus", 4.0}, {"salary", 68000.0}}};
    std::cout << "expression: " << expression << '\n';
    std::cout << "postfix:";
    for (const std::string& token : postfix) {
        std::cout << ' ' << token;
    }
    std::cout << "\nvalue: " << (eval_postfix(postfix, employee) != 0.0 ? "true" : "false") << "\n\n";

    const std::vector<Row> students = {
        {{{"id", 1.0}, {"name", std::string("Asha")}, {"age", 22.0}, {"gpa", 3.7}}},
        {{{"id", 2.0}, {"name", std::string("Kabir")}, {"age", 24.0}, {"gpa", 3.2}}},
        {{{"id", 3.0}, {"name", std::string("Meera")}, {"age", 21.0}, {"gpa", 3.9}}},
        {{{"id", 4.0}, {"name", std::string("Rohan")}, {"age", 27.0}, {"gpa", 2.8}}},
    };

    const std::vector<std::string> queries = {
        "SELECT id, name, gpa FROM students WHERE gpa >= 3.0 ORDER BY gpa DESC LIMIT 2",
        "SELECT id, name, age FROM students WHERE age >= 22 AND age <= 25 ORDER BY id ASC",
    };

    for (const std::string& sql : queries) {
        const SelectQuery query = parse_select(sql);
        const std::vector<Row> result = execute(query, students);
        std::cout << "sql: " << sql << '\n';
        print_rows(result, query.columns.empty() ? std::vector<std::string>{"id", "name", "age", "gpa"}
                                                 : query.columns);
        std::cout << '\n';
    }

    return 0;
}
