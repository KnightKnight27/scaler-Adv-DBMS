/**
 * @file main.cpp
 * @brief Evaluation of SQL WHERE clause conditions using Dijkstra's Shunting-Yard algorithm.
 *
 * This program tokenizes an infix SQL-like WHERE query string, converts it to postfix (RPN)
 * using the Shunting-Yard algorithm, and evaluates it against a list of employees.
 */

#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <algorithm>
#include <cctype>
#include <stdexcept>

/**
 * @brief Represents an Employee record.
 */
struct Employee {
    std::string name;
    int id = 0;
    int age = 0;
};

/**
 * @brief Returns precedence value of parsing operators.
 * Higher number indicates higher precedence.
 *
 * @param op Operator string.
 * @return int Precedence value (0 if not an operator).
 */
int getOperatorPrecedence(const std::string& op) {
    if (op == ">" || op == "<" || op == ">=" || op == "<=" || op == "=") return 3;
    if (op == "AND") return 2;
    if (op == "OR") return 1;
    return 0;
}

/**
 * @brief Checks if a string represents a valid integer.
 *
 * @param s String to check.
 * @return true if the string represents an integer, false otherwise.
 */
bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    size_t start = 0;
    if (s[0] == '-' && s.size() > 1) {
        start = 1;
    }
    return std::all_of(s.begin() + start, s.end(), [](unsigned char c) {
        return std::isdigit(c);
    });
}

/**
 * @brief Tokenizes the raw SQL-like WHERE query string.
 *
 * @param query Infix query string.
 * @return std::vector<std::string> List of tokens.
 */
std::vector<std::string> tokenize(const std::string& query) {
    std::vector<std::string> tokens;
    size_t i = 0;
    size_t n = query.size();

    while (i < n) {
        if (std::isspace(static_cast<unsigned char>(query[i]))) {
            i++;
            continue;
        }

        // Identifier or logical operator
        if (std::isalpha(static_cast<unsigned char>(query[i]))) {
            std::string current;
            while (i < n && (std::isalnum(static_cast<unsigned char>(query[i])) || query[i] == '_')) {
                current += query[i];
                i++;
            }
            // Check for logical operators in case-insensitive manner, converting to uppercase
            std::string upperStr = current;
            std::transform(upperStr.begin(), upperStr.end(), upperStr.begin(), [](unsigned char c) {
                return std::toupper(c);
            });
            if (upperStr == "AND" || upperStr == "OR") {
                tokens.push_back(upperStr);
            } else {
                tokens.push_back(current);
            }
        }
        // Numeric literals
        else if (std::isdigit(static_cast<unsigned char>(query[i]))) {
            std::string current;
            while (i < n && std::isdigit(static_cast<unsigned char>(query[i]))) {
                current += query[i];
                i++;
            }
            tokens.push_back(current);
        }
        // Two-character operators: >=, <=
        else if ((query[i] == '>' || query[i] == '<') && i + 1 < n && query[i + 1] == '=') {
            std::string op;
            op += query[i];
            op += '=';
            tokens.push_back(op);
            i += 2;
        }
        // Single-character operators/parentheses: >, <, =, (, )
        else {
            tokens.push_back(std::string(1, query[i]));
            i++;
        }
    }
    return tokens;
}

/**
 * @brief Converts infix expression tokens to postfix notation (RPN) using Shunting-Yard.
 *
 * @param tokens Infix tokens.
 * @return std::vector<std::string> Postfix tokens.
 */
std::vector<std::string> toPostfix(const std::vector<std::string>& tokens) {
    std::vector<std::string> postfix;
    std::stack<std::string> operatorStack;

    for (const auto& token : tokens) {
        if (token == "(") {
            operatorStack.push(token);
        } else if (token == ")") {
            while (!operatorStack.empty() && operatorStack.top() != "(") {
                postfix.push_back(operatorStack.top());
                operatorStack.pop();
            }
            if (operatorStack.empty()) {
                throw std::runtime_error("Mismatched parentheses: missing '('");
            }
            operatorStack.pop(); // Pop the '('
        } else if (getOperatorPrecedence(token) > 0) {
            while (!operatorStack.empty() && operatorStack.top() != "(" &&
                   getOperatorPrecedence(operatorStack.top()) >= getOperatorPrecedence(token)) {
                postfix.push_back(operatorStack.top());
                operatorStack.pop();
            }
            operatorStack.push(token);
        } else {
            postfix.push_back(token);
        }
    }

    while (!operatorStack.empty()) {
        if (operatorStack.top() == "(") {
            throw std::runtime_error("Mismatched parentheses: missing ')'");
        }
        postfix.push_back(operatorStack.top());
        operatorStack.pop();
    }

    return postfix;
}

/**
 * @brief Resolves database field values dynamically from Employee structure.
 *
 * @param field Name of the field.
 * @param employee Employee reference.
 * @return int Field value.
 */
int getFieldValue(const std::string& field, const Employee& employee) {
    if (field == "id") return employee.id;
    if (field == "age") return employee.age;
    throw std::runtime_error("Unknown identifier/column: '" + field + "'");
}

/**
 * @brief Evaluates postfix expression against a given Employee.
 *
 * @param postfix Postfix RPN expression.
 * @param employee Employee record to evaluate.
 * @return true if condition matches, false otherwise.
 */
bool evaluatePostfix(const std::vector<std::string>& postfix, const Employee& employee) {
    std::stack<int> valStack;

    for (const auto& token : postfix) {
        if (getOperatorPrecedence(token) == 0) {
            if (isNumber(token)) {
                valStack.push(std::stoi(token));
            } else {
                valStack.push(getFieldValue(token, employee));
            }
            continue;
        }

        if (valStack.size() < 2) {
            throw std::runtime_error("Malformed postfix expression: insufficient operands for operator '" + token + "'");
        }

        int operandB = valStack.top(); valStack.pop();
        int operandA = valStack.top(); valStack.pop();

        if (token == ">") {
            valStack.push(operandA > operandB);
        } else if (token == "<") {
            valStack.push(operandA < operandB);
        } else if (token == ">=") {
            valStack.push(operandA >= operandB);
        } else if (token == "<=") {
            valStack.push(operandA <= operandB);
        } else if (token == "=") {
            valStack.push(operandA == operandB);
        } else if (token == "AND") {
            valStack.push(operandA && operandB);
        } else if (token == "OR") {
            valStack.push(operandA || operandB);
        } else {
            throw std::runtime_error("Unsupported operator: '" + token + "'");
        }
    }

    if (valStack.size() != 1) {
        throw std::runtime_error("Malformed postfix expression: extra operands left on stack");
    }

    return valStack.top();
}

int main() {
    try {
        std::string query = "id > 3 AND (age < 25 OR age >= 30)";

        auto tokens = tokenize(query);
        auto postfix = toPostfix(tokens);

        std::cout << "Postfix: ";
        for (const auto& token : postfix) {
            std::cout << token << ' ';
        }
        std::cout << "\n\n";

        std::vector<Employee> employees = {
            {"Aryan", 1, 19},
            {"Riya", 2, 20},
            {"Karan", 3, 19},
            {"Sneha", 4, 21},
            {"Vivaan", 5, 20},
            {"Ishaan", 6, 31},
            {"Meera", 7, 22}
        };

        for (const auto& emp : employees) {
            if (evaluatePostfix(postfix, emp)) {
                std::cout << emp.name << " " << emp.id << " " << emp.age << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}