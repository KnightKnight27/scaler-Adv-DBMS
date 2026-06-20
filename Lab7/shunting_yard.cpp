#include <cctype>
#include <cmath>
#include <iostream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct OperatorAttr {
  int priority;
  bool isRightAssociative;
};

const std::unordered_map<std::string, OperatorAttr> operatorRegistry = {
    {"||", {1, false}}, {"&&", {2, false}}, {"=", {3, false}},
    {"!=", {3, false}}, {"<", {4, false}},  {">", {4, false}},
    {"<=", {4, false}}, {">=", {4, false}}, {"+", {5, false}},
    {"-", {5, false}},  {"*", {6, false}},  {"/", {6, false}},
    {"^", {7, true}},
};

std::vector<std::string> extractTokens(const std::string &expression) {
  std::vector<std::string> tokenList;
  int cursor = 0;
  const int exprLength = static_cast<int>(expression.size());

  while (cursor < exprLength) {
    const char currentChar = expression[cursor];

    if (std::isspace(static_cast<unsigned char>(currentChar))) {
      cursor++;
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(currentChar)) ||
        (currentChar == '.' && cursor + 1 < exprLength &&
         std::isdigit(static_cast<unsigned char>(expression[cursor + 1])))) {
      int scanIndex = cursor;
      while (scanIndex < exprLength &&
             (std::isdigit(static_cast<unsigned char>(expression[scanIndex])) ||
              expression[scanIndex] == '.')) {
        scanIndex++;
      }
      tokenList.push_back(expression.substr(cursor, scanIndex - cursor));
      cursor = scanIndex;
    } else if (std::isalpha(static_cast<unsigned char>(currentChar)) ||
               currentChar == '_') {
      int scanIndex = cursor;
      while (scanIndex < exprLength &&
             (std::isalnum(static_cast<unsigned char>(expression[scanIndex])) ||
              expression[scanIndex] == '_')) {
        scanIndex++;
      }
      tokenList.push_back(expression.substr(cursor, scanIndex - cursor));
      cursor = scanIndex;
    } else if (currentChar == '(' || currentChar == ')') {
      tokenList.push_back(std::string(1, currentChar));
      cursor++;
    } else {
      if (cursor + 1 < exprLength) {
        std::string dualCharOp = expression.substr(cursor, 2);
        if (operatorRegistry.count(dualCharOp)) {
          tokenList.push_back(dualCharOp);
          cursor += 2;
          continue;
        }
      }
      tokenList.push_back(std::string(1, currentChar));
      cursor++;
    }
  }
  return tokenList;
}

std::vector<std::string>
convertIntoRPN(const std::vector<std::string> &infixTokens) {
  std::vector<std::string> postfixList;
  std::stack<std::string> operatorStack;

  for (const auto &element : infixTokens) {
    if (element == "(") {
      operatorStack.push(element);
    } else if (element == ")") {
      while (!operatorStack.empty() && operatorStack.top() != "(") {
        postfixList.push_back(operatorStack.top());
        operatorStack.pop();
      }
      if (operatorStack.empty()) {
        throw std::runtime_error(
            "Syntax Error: Mismatched parentheses detected");
      }
      operatorStack.pop(); // Remove '('
    } else if (operatorRegistry.count(element)) {
      const auto &currentOp = operatorRegistry.at(element);
      while (!operatorStack.empty() &&
             operatorRegistry.count(operatorStack.top())) {
        const auto &topOp = operatorRegistry.at(operatorStack.top());
        if (topOp.priority > currentOp.priority ||
            (topOp.priority == currentOp.priority &&
             !currentOp.isRightAssociative)) {
          postfixList.push_back(operatorStack.top());
          operatorStack.pop();
        } else {
          break;
        }
      }
      operatorStack.push(element);
    } else {
      postfixList.push_back(element);
    }
  }

  while (!operatorStack.empty()) {
    if (operatorStack.top() == "(") {
      throw std::runtime_error("Syntax Error: Mismatched parentheses detected");
    }
    postfixList.push_back(operatorStack.top());
    operatorStack.pop();
  }
  return postfixList;
}

double
evaluateRPN(const std::vector<std::string> &postfixTokens,
            const std::unordered_map<std::string, double> &variableBindings) {
  std::stack<double> operandStack;

  for (const auto &token : postfixTokens) {
    if (operatorRegistry.count(token)) {
      if (operandStack.size() < 2) {
        throw std::runtime_error(
            "Evaluation Error: Insufficient operands on stack");
      }
      double rightOperand = operandStack.top();
      operandStack.pop();
      double leftOperand = operandStack.top();
      operandStack.pop();

      if (token == "+")
        operandStack.push(leftOperand + rightOperand);
      else if (token == "-")
        operandStack.push(leftOperand - rightOperand);
      else if (token == "*")
        operandStack.push(leftOperand * rightOperand);
      else if (token == "/")
        operandStack.push(leftOperand / rightOperand);
      else if (token == "^")
        operandStack.push(std::pow(leftOperand, rightOperand));
      else if (token == "<")
        operandStack.push(leftOperand < rightOperand ? 1.0 : 0.0);
      else if (token == ">")
        operandStack.push(leftOperand > rightOperand ? 1.0 : 0.0);
      else if (token == "<=")
        operandStack.push(leftOperand <= rightOperand ? 1.0 : 0.0);
      else if (token == ">=")
        operandStack.push(leftOperand >= rightOperand ? 1.0 : 0.0);
      else if (token == "=")
        operandStack.push(leftOperand == rightOperand ? 1.0 : 0.0);
      else if (token == "!=")
        operandStack.push(leftOperand != rightOperand ? 1.0 : 0.0);
      else if (token == "&&")
        operandStack.push((leftOperand && rightOperand) ? 1.0 : 0.0);
      else if (token == "||")
        operandStack.push((leftOperand || rightOperand) ? 1.0 : 0.0);
      else
        throw std::runtime_error("Evaluation Error: Unrecognized operator: " +
                                 token);
    } else {
      try {
        operandStack.push(std::stod(token));
      } catch (...) {
        auto it = variableBindings.find(token);
        if (it == variableBindings.end()) {
          throw std::runtime_error(
              "Evaluation Error: Variable not found in scope: " + token);
        }
        operandStack.push(it->second);
      }
    }
  }

  if (operandStack.empty()) {
    throw std::runtime_error("Evaluation Error: Empty evaluation stack");
  }
  return operandStack.top();
}

int runShuntingYardDemo() {
  std::string expr = "age * 2 + salary / 1000 > 100";
  auto tokens = extractTokens(expr);
  auto rpn = convertIntoRPN(tokens);

  std::cout << "Expression : " << expr << "\n";
  std::cout << "RPN        : ";
  for (size_t idx = 0; idx < rpn.size(); ++idx) {
    std::cout << rpn[idx] << " ";
  }
  std::cout << "\n";

  std::unordered_map<std::string, double> sampleVars;
  sampleVars["age"] = 30;
  sampleVars["salary"] = 50000;
  double outcome = evaluateRPN(rpn, sampleVars);
  std::cout << "Result     : " << (outcome ? "true" : "false") << "\n";

  return 0;
}