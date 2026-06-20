#include <cctype>
#include <iostream>
#include <stdexcept>
#include <stack>
#include <string>
#include <vector>

struct Token {
    std::string text;
    bool is_operator = false;
};

std::string upper_copy(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

int precedence(const std::string& op) {
    if (op == ">" || op == "<" || op == ">=" || op == "<=" || op == "=" || op == "!=") {
        return 3;
    }
    if (op == "AND") {
        return 2;
    }
    if (op == "OR") {
        return 1;
    }
    return 0;
}

bool operator_token(const std::string& text) {
    return precedence(text) > 0;
}

std::vector<Token> tokenize(const std::string& input) {
    std::vector<Token> tokens;

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
            std::string upper = upper_copy(word);
            if (upper == "AND" || upper == "OR") {
                tokens.push_back({upper, true});
            } else {
                tokens.push_back({word, false});
            }
            continue;
        }

        if (std::isdigit(ch)) {
            std::size_t start = i++;
            while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) {
                ++i;
            }
            tokens.push_back({input.substr(start, i - start), false});
            continue;
        }

        if (i + 1 < input.size()) {
            const std::string two = input.substr(i, 2);
            if (operator_token(two)) {
                tokens.push_back({two, true});
                i += 2;
                continue;
            }
        }

        const std::string one(1, input[i]);
        if (operator_token(one)) {
            tokens.push_back({one, true});
        } else if (one == "(" || one == ")") {
            tokens.push_back({one, false});
        } else {
            throw std::runtime_error("unexpected character in expression: " + one);
        }
        ++i;
    }

    return tokens;
}

std::vector<Token> to_postfix(const std::vector<Token>& infix) {
    std::vector<Token> output;
    std::stack<Token> operators;

    for (const Token& token : infix) {
        if (token.text == "(") {
            operators.push(token);
        } else if (token.text == ")") {
            while (!operators.empty() && operators.top().text != "(") {
                output.push_back(operators.top());
                operators.pop();
            }
            if (operators.empty()) {
                throw std::runtime_error("mismatched parenthesis");
            }
            operators.pop();
        } else if (token.is_operator) {
            while (!operators.empty() && operators.top().is_operator &&
                   precedence(operators.top().text) >= precedence(token.text)) {
                output.push_back(operators.top());
                operators.pop();
            }
            operators.push(token);
        } else {
            output.push_back(token);
        }
    }

    while (!operators.empty()) {
        if (operators.top().text == "(") {
            throw std::runtime_error("mismatched parenthesis");
        }
        output.push_back(operators.top());
        operators.pop();
    }

    return output;
}

struct Student {
    int id = 0;
    std::string name;
    int age = 0;
    int marks = 0;
};

bool is_number(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

int column_value(const Student& row, const std::string& column) {
    if (column == "id") {
        return row.id;
    }
    if (column == "age") {
        return row.age;
    }
    if (column == "marks") {
        return row.marks;
    }
    throw std::runtime_error("unknown numeric column: " + column);
}

int operand_value(const Student& row, const std::string& token) {
    return is_number(token) ? std::stoi(token) : column_value(row, token);
}

bool evaluate_postfix(const std::vector<Token>& postfix, const Student& row) {
    std::stack<int> values;

    for (const Token& token : postfix) {
        if (!token.is_operator) {
            values.push(operand_value(row, token.text));
            continue;
        }

        if (values.size() < 2) {
            throw std::runtime_error("not enough operands for " + token.text);
        }
        int rhs = values.top();
        values.pop();
        int lhs = values.top();
        values.pop();

        if (token.text == ">") values.push(lhs > rhs);
        else if (token.text == "<") values.push(lhs < rhs);
        else if (token.text == ">=") values.push(lhs >= rhs);
        else if (token.text == "<=") values.push(lhs <= rhs);
        else if (token.text == "=") values.push(lhs == rhs);
        else if (token.text == "!=") values.push(lhs != rhs);
        else if (token.text == "AND") values.push(lhs != 0 && rhs != 0);
        else if (token.text == "OR") values.push(lhs != 0 || rhs != 0);
    }

    if (values.size() != 1) {
        throw std::runtime_error("invalid postfix expression");
    }
    return values.top() != 0;
}

int main() {
    const std::string where_clause = "marks >= 80 AND (age < 23 OR id > 4)";
    const std::vector<Student> students = {
        {1, "Asha", 22, 91},
        {2, "Kabir", 24, 76},
        {3, "Meera", 21, 88},
        {4, "Rohan", 25, 82},
        {5, "Diya", 23, 93},
    };

    const std::vector<Token> postfix = to_postfix(tokenize(where_clause));

    std::cout << "infix:   " << where_clause << '\n';
    std::cout << "postfix:";
    for (const Token& token : postfix) {
        std::cout << ' ' << token.text;
    }
    std::cout << "\n\nmatching rows:\n";

    for (const Student& row : students) {
        if (evaluate_postfix(postfix, row)) {
            std::cout << "  " << row.name << " id=" << row.id
                      << " age=" << row.age << " marks=" << row.marks << '\n';
        }
    }

    return 0;
}
