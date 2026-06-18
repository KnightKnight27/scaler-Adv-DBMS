#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct Employee {
    std::string name;
    int id;
    int age;
};

struct OpInfo {
    int precedence;
    bool right_assoc;
};

const std::unordered_map<std::string, OpInfo>& operatorTable() {
    static const std::unordered_map<std::string, OpInfo> ops = {
        {"OR", {1, false}},
        {"AND", {2, false}},
        {"=", {3, false}},
        {"!=", {3, false}},
        {"<", {4, false}},
        {">", {4, false}},
        {"<=", {4, false}},
        {">=", {4, false}}
    };
    return ops;
}

bool isOperator(const std::string& token) {
    return operatorTable().count(token);
}

int precedence(const std::string& token) {
    auto it = operatorTable().find(token);
    return (it == operatorTable().end()) ? 0 : it->second.precedence;
}

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

bool isIntegerLiteral(const std::string& s) {
    if (s.empty()) return false;

    size_t start = (s[0] == '-') ? 1 : 0;

    for (size_t i = start; i < s.size(); i++) {
        if (!std::isdigit(s[i]))
            return false;
    }
    return true;
}

std::vector<std::string> tokenize(const std::string& expr) {
    std::vector<std::string> tokens;
    size_t i = 0;

    while (i < expr.size()) {

        if (std::isspace(expr[i])) {
            i++;
            continue;
        }

        if (std::isalpha(expr[i]) || expr[i] == '_') {
            std::string word;

            while (i < expr.size() &&
                   (std::isalnum(expr[i]) || expr[i] == '_')) {
                word += expr[i++];
            }

            std::string upper = toUpper(word);

            if (upper == "AND" || upper == "OR")
                tokens.push_back(upper);
            else
                tokens.push_back(word);

            continue;
        }

        if (std::isdigit(expr[i])) {
            std::string num;

            while (i < expr.size() && std::isdigit(expr[i])) {
                num += expr[i++];
            }

            tokens.push_back(num);
            continue;
        }

        if ((expr[i] == '<' || expr[i] == '>' || expr[i] == '!') &&
            i + 1 < expr.size() && expr[i + 1] == '=') {

            tokens.push_back(
                std::string(1, expr[i]) + expr[i + 1]);

            i += 2;
            continue;
        }

        if (expr[i] == '&' && i + 1 < expr.size() &&
            expr[i + 1] == '&') {
            tokens.push_back("AND");
            i += 2;
            continue;
        }

        if (expr[i] == '|' && i + 1 < expr.size() &&
            expr[i + 1] == '|') {
            tokens.push_back("OR");
            i += 2;
            continue;
        }

        tokens.push_back(std::string(1, expr[i]));
        i++;
    }

    return tokens;
}

std::vector<std::string> toPostfix(
    const std::vector<std::string>& tokens) {

    std::vector<std::string> output;
    std::stack<std::string> operators;

    for (const auto& token : tokens) {

        if (token == "(") {
            operators.push(token);
        }

        else if (token == ")") {

            while (!operators.empty() &&
                   operators.top() != "(") {
                output.push_back(operators.top());
                operators.pop();
            }

            if (operators.empty())
                throw std::runtime_error(
                    "Mismatched parentheses");

            operators.pop();
        }

        else if (isOperator(token)) {

            while (!operators.empty() &&
                   operators.top() != "(" &&
                   precedence(operators.top()) >=
                       precedence(token)) {

                output.push_back(operators.top());
                operators.pop();
            }

            operators.push(token);
        }

        else {
            output.push_back(token);
        }
    }

    while (!operators.empty()) {

        if (operators.top() == "(")
            throw std::runtime_error(
                "Mismatched parentheses");

        output.push_back(operators.top());
        operators.pop();
    }

    return output;
}

int resolveField(const std::string& field,
                 const Employee& emp) {

    if (field == "id")
        return emp.id;

    if (field == "age")
        return emp.age;

    throw std::runtime_error(
        "Unknown column: " + field);
}

bool evaluatePostfix(
    const std::vector<std::string>& postfix,
    const Employee& emp) {

    std::stack<int> st;

    for (const auto& token : postfix) {

        if (!isOperator(token)) {

            if (isIntegerLiteral(token))
                st.push(std::stoi(token));
            else
                st.push(resolveField(token, emp));

            continue;
        }

        int rhs = st.top();
        st.pop();

        int lhs = st.top();
        st.pop();

        if (token == ">")
            st.push(lhs > rhs);

        else if (token == "<")
            st.push(lhs < rhs);

        else if (token == ">=")
            st.push(lhs >= rhs);

        else if (token == "<=")
            st.push(lhs <= rhs);

        else if (token == "=")
            st.push(lhs == rhs);

        else if (token == "!=")
            st.push(lhs != rhs);

        else if (token == "AND")
            st.push(lhs && rhs);

        else if (token == "OR")
            st.push(lhs || rhs);
    }

    return st.top() != 0;
}

std::string extractWhereClause(
    const std::string& sql) {

    std::string upper = toUpper(sql);

    size_t pos = upper.find("WHERE");

    if (pos == std::string::npos)
        throw std::runtime_error(
            "No WHERE clause found.");

    std::string clause =
        sql.substr(pos + 5);

    if (!clause.empty() &&
        clause.back() == ';')
        clause.pop_back();

    return clause;
}

int main() {

    std::vector<Employee> employees = {
        {"Rama", 1, 19},
        {"Aarav", 2, 20},
        {"Karan", 3, 19},
        {"Sneha", 4, 21},
        {"Vivaan", 5, 20},
        {"Ishaan", 6, 31},
        {"Meera", 7, 22},
        {"Devansh", 8, 33}
    };

    try {

        std::string sql;

        std::cout
            << "Enter SQL Query:\n";

        std::getline(std::cin, sql);

        std::string whereClause =
            extractWhereClause(sql);

        auto tokens =
            tokenize(whereClause);

        auto postfix =
            toPostfix(tokens);

        std::cout << "\nWHERE Clause:\n"
                  << whereClause << "\n";

        std::cout << "\nTokens:\n";

        for (auto& t : tokens)
            std::cout << t << " ";

        std::cout << "\n";

        std::cout << "\nPostfix Expression:\n";

        for (auto& t : postfix)
            std::cout << t << " ";

        std::cout << "\n";

        std::cout
            << "\nMatching Employees:\n\n";

        for (const auto& emp : employees) {

            if (evaluatePostfix(
                    postfix, emp)) {

                std::cout
                    << "ID: " << emp.id
                    << " | Age: " << emp.age
                    << " | Name: "
                    << emp.name
                    << '\n';
            }
        }
    }

    catch (const std::exception& e) {

        std::cerr
            << "\nError: "
            << e.what()
            << '\n';
    }

    return 0;
}