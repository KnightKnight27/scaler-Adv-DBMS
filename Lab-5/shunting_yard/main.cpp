#include <cctype>
#include <cmath>
#include <iostream>
#include <queue>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

struct Token {
    enum Type { NUMBER, OPERATOR, LPAREN, RPAREN, END };
    Type type;
    double value;
    char op;

    Token() : type(END), value(0), op('\0') {}
    Token(Type t, double v) : type(t), value(v), op('\0') {}
    Token(Type t, char o) : type(t), value(0), op(o) {}
};

class Tokenizer {
public:
    std::vector<Token> tokenize(const std::string &expr) {
        std::vector<Token> tokens;
        for (std::size_t i = 0; i < expr.length(); ++i) {
            char c = expr[i];
            if (std::isspace(c)) continue;

            if (std::isdigit(c) || (c == '.' && i + 1 < expr.length() && std::isdigit(expr[i + 1]))) {
                double num = 0;
                while (i < expr.length() && (std::isdigit(expr[i]) || expr[i] == '.')) {
                    num = num * 10 + (expr[i] - '0');
                    ++i;
                }
                --i;
                tokens.push_back(Token(Token::NUMBER, num));
            } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
                tokens.push_back(Token(Token::OPERATOR, c));
            } else if (c == '(') {
                tokens.push_back(Token(Token::LPAREN, '('));
            } else if (c == ')') {
                tokens.push_back(Token(Token::RPAREN, ')'));
            }
        }
        return tokens;
    }
};

class ShuntingYard {
public:
    std::queue<Token> infixToPostfix(const std::vector<Token> &infix) {
        std::queue<Token> output;
        std::stack<Token> opStack;

        for (const auto &token : infix) {
            if (token.type == Token::NUMBER) {
                output.push(token);
            } else if (token.type == Token::OPERATOR) {
                while (!opStack.empty() && opStack.top().type == Token::OPERATOR &&
                       precedence(opStack.top().op) >= precedence(token.op)) {
                    output.push(opStack.top());
                    opStack.pop();
                }
                opStack.push(token);
            } else if (token.type == Token::LPAREN) {
                opStack.push(token);
            } else if (token.type == Token::RPAREN) {
                while (!opStack.empty() && opStack.top().type != Token::LPAREN) {
                    output.push(opStack.top());
                    opStack.pop();
                }
                if (!opStack.empty()) opStack.pop();
            }
        }

        while (!opStack.empty()) {
            output.push(opStack.top());
            opStack.pop();
        }

        return output;
    }

    double evaluate(std::queue<Token> postfix) {
        std::stack<double> evalStack;

        while (!postfix.empty()) {
            Token token = postfix.front();
            postfix.pop();

            if (token.type == Token::NUMBER) {
                evalStack.push(token.value);
            } else if (token.type == Token::OPERATOR) {
                double b = evalStack.top();
                evalStack.pop();
                double a = evalStack.top();
                evalStack.pop();

                switch (token.op) {
                case '+': evalStack.push(a + b); break;
                case '-': evalStack.push(a - b); break;
                case '*': evalStack.push(a * b); break;
                case '/': evalStack.push(a / b); break;
                case '^': evalStack.push(std::pow(a, b)); break;
                }
            }
        }

        return evalStack.top();
    }

private:
    int precedence(char op) const {
        if (op == '+' || op == '-') return 1;
        if (op == '*' || op == '/') return 2;
        if (op == '^') return 3;
        return 0;
    }
};

int main() {
    Tokenizer tokenizer;
    ShuntingYard shuntingYard;

    const std::string expressions[] = {
        "3 + 4 * 2",
        "( 3 + 4 ) * 2",
        "10 / 2 + 3",
        "2 ^ 3 + 5",
        "( ( 15 ) ) + 5 * 2",
    };

    for (const auto &expr : expressions) {
        auto tokens = tokenizer.tokenize(expr);
        auto postfix = shuntingYard.infixToPostfix(tokens);
        double result = shuntingYard.evaluate(postfix);
        std::cout << "Expression: " << expr << " = " << result << '\n';
    }

    return 0;
}
