#include <iostream>
#include <sstream>
#include <stack>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <cctype>
#include <cmath>

struct OperatorMeta
{
    int opPrecedence;
    bool isRightAssoc;
};

const std::unordered_map<std::string, OperatorMeta> OPERATOR_METADATA = {
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

std::vector<std::string> tokenizeExpr(const std::string &expression)
{
    std::vector<std::string> tokens;
    int idx = 0;
    int length = static_cast<int>(expression.size());
    while (idx < length)
    {
        if (std::isspace(static_cast<unsigned char>(expression[idx])))
        {
            idx++;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(expression[idx])) || 
            (expression[idx] == '.' && idx + 1 < length && std::isdigit(static_cast<unsigned char>(expression[idx + 1]))))
        {
            int nextIdx = idx;
            while (nextIdx < length && (std::isdigit(static_cast<unsigned char>(expression[nextIdx])) || expression[nextIdx] == '.'))
                nextIdx++;
            tokens.push_back(expression.substr(idx, nextIdx - idx));
            idx = nextIdx;
        }
        else if (std::isalpha(static_cast<unsigned char>(expression[idx])) || expression[idx] == '_')
        {
            int nextIdx = idx;
            while (nextIdx < length && (std::isalnum(static_cast<unsigned char>(expression[nextIdx])) || expression[nextIdx] == '_'))
                nextIdx++;
            tokens.push_back(expression.substr(idx, nextIdx - idx));
            idx = nextIdx;
        }
        else if (expression[idx] == '(' || expression[idx] == ')')
        {
            tokens.push_back(std::string(1, expression[idx++]));
        }
        else
        {
            if (idx + 1 < length)
            {
                std::string doubleOp = expression.substr(idx, 2);
                if (OPERATOR_METADATA.count(doubleOp))
                {
                    tokens.push_back(doubleOp);
                    idx += 2;
                    continue;
                }
            }
            tokens.push_back(std::string(1, expression[idx++]));
        }
    }
    return tokens;
}

std::vector<std::string> shuntingYardRPN(const std::vector<std::string> &tokens)
{
    std::vector<std::string> postfixQueue;
    std::stack<std::string> operatorStack;

    for (const auto &token : tokens)
    {
        if (token == "(")
        {
            operatorStack.push(token);
        }
        else if (token == ")")
        {
            while (!operatorStack.empty() && operatorStack.top() != "(")
            {
                postfixQueue.push_back(operatorStack.top());
                operatorStack.pop();
            }
            if (operatorStack.empty())
                throw std::runtime_error("Error: Mismatched parentheses in expression");
            operatorStack.pop();
        }
        else if (OPERATOR_METADATA.count(token))
        {
            const auto &currentOp = OPERATOR_METADATA.at(token);
            while (!operatorStack.empty() && OPERATOR_METADATA.count(operatorStack.top()))
            {
                const auto &topOp = OPERATOR_METADATA.at(operatorStack.top());
                if (topOp.opPrecedence > currentOp.opPrecedence ||
                    (topOp.opPrecedence == currentOp.opPrecedence && !currentOp.isRightAssoc))
                {
                    postfixQueue.push_back(operatorStack.top());
                    operatorStack.pop();
                }
                else
                    break;
            }
            operatorStack.push(token);
        }
        else
        {
            postfixQueue.push_back(token);
        }
    }
    while (!operatorStack.empty())
    {
        if (operatorStack.top() == "(")
            throw std::runtime_error("Error: Mismatched parentheses in expression");
        postfixQueue.push_back(operatorStack.top());
        operatorStack.pop();
    }
    return postfixQueue;
}

double evaluateRPN(const std::vector<std::string> &postfix, const std::unordered_map<std::string, double> &variables)
{
    std::stack<double> evalStack;
    for (const auto &token : postfix)
    {
        if (OPERATOR_METADATA.count(token))
        {
            double rightOperand = evalStack.top();
            evalStack.pop();
            double leftOperand = evalStack.top();
            evalStack.pop();
            if (token == "+")
                evalStack.push(leftOperand + rightOperand);
            else if (token == "-")
                evalStack.push(leftOperand - rightOperand);
            else if (token == "*")
                evalStack.push(leftOperand * rightOperand);
            else if (token == "/")
                evalStack.push(leftOperand / rightOperand);
            else if (token == "^")
                evalStack.push(std::pow(leftOperand, rightOperand));
            else if (token == "<")
                evalStack.push(leftOperand < rightOperand ? 1.0 : 0.0);
            else if (token == ">")
                evalStack.push(leftOperand > rightOperand ? 1.0 : 0.0);
            else if (token == "<=")
                evalStack.push(leftOperand <= rightOperand ? 1.0 : 0.0);
            else if (token == ">=")
                evalStack.push(leftOperand >= rightOperand ? 1.0 : 0.0);
            else if (token == "=")
                evalStack.push(leftOperand == rightOperand ? 1.0 : 0.0);
            else if (token == "!=")
                evalStack.push(leftOperand != rightOperand ? 1.0 : 0.0);
            else if (token == "&&")
                evalStack.push((leftOperand && rightOperand) ? 1.0 : 0.0);
            else if (token == "||")
                evalStack.push((leftOperand || rightOperand) ? 1.0 : 0.0);
        }
        else
        {
            try
            {
                evalStack.push(std::stod(token));
            }
            catch (...)
            {
                auto it = variables.find(token);
                if (it == variables.end())
                    throw std::runtime_error("Error: Unknown variable encountered: " + token);
                evalStack.push(it->second);
            }
        }
    }
    return evalStack.top();
}

int runShuntingYardDemo()
{
    std::string expr = "age * 2 + salary / 1000 > 100";
    auto tokens = tokenizeExpr(expr);
    auto rpn = shuntingYardRPN(tokens);

    std::cout << "Expression : " << expr << "\n";
    std::cout << "RPN        : ";
    for (size_t i = 0; i < rpn.size(); ++i)
        std::cout << rpn[i] << " ";
    std::cout << "\n";

    std::unordered_map<std::string, double> vars;
    vars["age"] = 30;
    vars["salary"] = 50000;
    double result = evaluateRPN(rpn, vars);
    std::cout << "Result     : " << (result ? "true" : "false") << "\n";

    return 0;
}