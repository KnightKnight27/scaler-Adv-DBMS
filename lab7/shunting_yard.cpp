#include <iostream>
#include <vector>
#include <string>
#include <stack>
#include <unordered_map>
#include <stdexcept>
#include <cctype>
#include <cmath>

struct OperatorData
{
    int priority;
    bool rightAssociative;
};

const std::unordered_map<std::string, OperatorData> OPERATORS =
{
    {"||",{1,false}},
    {"&&",{2,false}},
    {"=",{3,false}},
    {"!=",{3,false}},
    {"<",{4,false}},
    {">",{4,false}},
    {"<=",{4,false}},
    {">=",{4,false}},
    {"+",{5,false}},
    {"-",{5,false}},
    {"*",{6,false}},
    {"/",{6,false}},
    {"^",{7,true}}
};

bool isOperator(const std::string& token)
{
    return OPERATORS.find(token) != OPERATORS.end();
}

std::vector<std::string> splitExpression(
    const std::string& expression)
{
    std::vector<std::string> parts;

    std::size_t pos = 0;

    while (pos < expression.size())
    {
        char current = expression[pos];

        if (std::isspace(static_cast<unsigned char>(current)))
        {
            ++pos;
            continue;
        }

        if (std::isdigit(current) ||
           (current == '.' &&
            pos + 1 < expression.size() &&
            std::isdigit(expression[pos + 1])))
        {
            std::size_t start = pos;

            while (pos < expression.size() &&
                  (std::isdigit(expression[pos]) ||
                   expression[pos] == '.'))
            {
                ++pos;
            }

            parts.push_back(
                expression.substr(start, pos - start));
        }
        else if (std::isalpha(current) || current == '_')
        {
            std::size_t start = pos;

            while (pos < expression.size() &&
                  (std::isalnum(expression[pos]) ||
                   expression[pos] == '_'))
            {
                ++pos;
            }

            parts.push_back(
                expression.substr(start, pos - start));
        }
        else if (current == '(' || current == ')')
        {
            parts.emplace_back(1, current);
            ++pos;
        }
        else
        {
            if (pos + 1 < expression.size())
            {
                std::string twoChar =
                    expression.substr(pos, 2);

                if (isOperator(twoChar))
                {
                    parts.push_back(twoChar);
                    pos += 2;
                    continue;
                }
            }

            parts.emplace_back(1, current);
            ++pos;
        }
    }

    return parts;
}

std::vector<std::string> buildPostfix(
    const std::vector<std::string>& infix)
{
    std::vector<std::string> postfix;
    std::stack<std::string> operatorStack;

    for (const std::string& token : infix)
    {
        if (token == "(")
        {
            operatorStack.push(token);
        }
        else if (token == ")")
        {
            while (!operatorStack.empty() &&
                   operatorStack.top() != "(")
            {
                postfix.push_back(
                    operatorStack.top());

                operatorStack.pop();
            }

            if (operatorStack.empty())
            {
                throw std::runtime_error(
                    "Parenthesis mismatch");
            }

            operatorStack.pop();
        }
        else if (isOperator(token))
        {
            const auto& current =
                OPERATORS.at(token);

            while (!operatorStack.empty() &&
                   isOperator(operatorStack.top()))
            {
                const auto& top =
                    OPERATORS.at(operatorStack.top());

                bool shouldPop =
                    top.priority > current.priority ||
                    (top.priority ==
                     current.priority &&
                     !current.rightAssociative);

                if (!shouldPop)
                    break;

                postfix.push_back(
                    operatorStack.top());

                operatorStack.pop();
            }

            operatorStack.push(token);
        }
        else
        {
            postfix.push_back(token);
        }
    }

    while (!operatorStack.empty())
    {
        if (operatorStack.top() == "(")
        {
            throw std::runtime_error(
                "Parenthesis mismatch");
        }

        postfix.push_back(operatorStack.top());
        operatorStack.pop();
    }

    return postfix;
}

double evaluatePostfix(
    const std::vector<std::string>& postfix,
    const std::unordered_map<std::string,double>& variables)
{
    std::stack<double> values;

    for (const std::string& token : postfix)
    {
        if (!isOperator(token))
        {
            try
            {
                values.push(std::stod(token));
            }
            catch (...)
            {
                auto itr = variables.find(token);

                if (itr == variables.end())
                {
                    throw std::runtime_error(
                        "Undefined identifier: " + token);
                }

                values.push(itr->second);
            }

            continue;
        }

        double rhs = values.top();
        values.pop();

        double lhs = values.top();
        values.pop();

        if (token == "+")
            values.push(lhs + rhs);
        else if (token == "-")
            values.push(lhs - rhs);
        else if (token == "*")
            values.push(lhs * rhs);
        else if (token == "/")
            values.push(lhs / rhs);
        else if (token == "^")
            values.push(std::pow(lhs, rhs));
        else if (token == "<")
            values.push(lhs < rhs);
        else if (token == ">")
            values.push(lhs > rhs);
        else if (token == "<=")
            values.push(lhs <= rhs);
        else if (token == ">=")
            values.push(lhs >= rhs);
        else if (token == "=")
            values.push(lhs == rhs);
        else if (token == "!=")
            values.push(lhs != rhs);
        else if (token == "&&")
            values.push(lhs && rhs);
        else if (token == "||")
            values.push(lhs || rhs);
    }

    return values.top();
}

void demonstrateShuntingYard()
{
    std::string expression =
        "age * 2 + salary / 1000 > 100";

    auto infixTokens =
        splitExpression(expression);

    auto postfixTokens =
        buildPostfix(infixTokens);

    std::cout << "Expression: "
              << expression
              << '\n';

    std::cout << "Postfix: ";

    for (const auto& token : postfixTokens)
    {
        std::cout << token << ' ';
    }

    std::cout << '\n';

    std::unordered_map<std::string,double> values =
    {
        {"age",30},
        {"salary",50000}
    };

    bool answer =
        evaluatePostfix(
            postfixTokens,
            values);

    std::cout
        << "Result: "
        << (answer ? "true" : "false")
        << "\n\n";
}