/**
 * @file main.cpp
 * @brief AST-based parser and evaluator for simplified SQL SELECT statements.
 *
 * This program tokenizes a SELECT query, parses it into an AST (Abstract Syntax
 * Tree) with robust memory safety (std::unique_ptr), and evaluates the
 * condition against a set of Employee records.
 */

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief Represents an Employee record.
 */
struct Employee {
  std::string name;
  int id = 0;
  int age = 0;
};

/**
 * @brief Represents a Node in the Query AST.
 * Uses std::unique_ptr for automated, leak-free memory management.
 */
struct Node {
  std::string op;
  std::string col;
  int val = 0;
  std::unique_ptr<Node> l = nullptr;
  std::unique_ptr<Node> r = nullptr;
};

/**
 * @brief Represents a single lexical token.
 */
struct Token {
  std::string text;
};

/**
 * @brief Lexical analyzer to tokenize a SQL query.
 *
 * @param query The raw SQL statement string.
 * @return std::vector<Token> List of parsed tokens.
 */
std::vector<Token> tokenize(const std::string &query) {
  std::vector<Token> tokens;
  size_t i = 0;
  size_t n = query.size();

  while (i < n) {
    if (std::isspace(static_cast<unsigned char>(query[i]))) {
      i++;
      continue;
    }

    if (std::isalpha(static_cast<unsigned char>(query[i]))) {
      std::string current;
      while (i < n && (std::isalnum(static_cast<unsigned char>(query[i])) ||
                       query[i] == '_')) {
        current += query[i];
        i++;
      }
      tokens.push_back({current});
    } else if (std::isdigit(static_cast<unsigned char>(query[i]))) {
      std::string current;
      while (i < n && std::isdigit(static_cast<unsigned char>(query[i]))) {
        current += query[i];
        i++;
      }
      tokens.push_back({current});
    } else if ((query[i] == '>' || query[i] == '<') && i + 1 < n &&
               query[i + 1] == '=') {
      std::string op;
      op += query[i];
      op += '=';
      tokens.push_back({op});
      i += 2;
    } else {
      tokens.push_back({std::string(1, query[i])});
      i++;
    }
  }
  return tokens;
}

/**
 * @brief Represents the final parsed components of the SELECT query.
 */
struct ParsedQuery {
  std::string selectedColumn;
  std::string tableName;
  std::unique_ptr<Node> whereRoot;
};

/**
 * @brief Recursive-descent parser for the simple query syntax.
 */
struct Parser {
  std::vector<Token> tokens;
  size_t cursor = 0;

  explicit Parser(std::vector<Token> t) : tokens(std::move(t)) {}

  /**
   * @brief Helper to convert a string to uppercase.
   */
  std::string toUppercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
  }

  /**
   * @brief Consumes and returns the current token, advancing the parser
   * cursor.
   */
  std::string eat() {
    if (cursor >= tokens.size()) {
      throw std::runtime_error("Unexpected end of tokens in query parser");
    }
    return tokens[cursor++].text;
  }

  /**
   * @brief Parses a base comparison condition (e.g., id >= 3).
   */
  std::unique_ptr<Node> parseCondition() {
    if (cursor + 2 >= tokens.size()) {
      throw std::runtime_error("Malformed WHERE condition expression");
    }
    std::string col = eat();
    std::string op = eat();
    int val = std::stoi(eat());

    auto node = std::make_unique<Node>();
    node->op = op;
    node->col = col;
    node->val = val;
    return node;
  }

  /**
   * @brief Parses an expression with potential parenthesis and logical
   * operators (OR).
   */
  std::unique_ptr<Node> parseExpression() {
    std::unique_ptr<Node> curNode;

    if (cursor >= tokens.size()) {
      throw std::runtime_error("Expected expression condition in WHERE clause");
    }

    if (tokens[cursor].text == "(") {
      eat(); // eat "("
      curNode = parseExpression();
      if (cursor >= tokens.size() || tokens[cursor].text != ")") {
        throw std::runtime_error("Mismatched parenthesis: expected ')'");
      }
      eat(); // eat ")"
    } else {
      curNode = parseCondition();
    }

    while (cursor < tokens.size() &&
           toUppercase(tokens[cursor].text) == "OR") {
      eat(); // eat "OR"

      std::unique_ptr<Node> rightExpr;
      if (cursor < tokens.size() && tokens[cursor].text == "(") {
        eat(); // eat "("
        rightExpr = parseExpression();
        if (cursor >= tokens.size() || tokens[cursor].text != ")") {
          throw std::runtime_error("Mismatched parenthesis: expected ')'");
        }
        eat(); // eat ")"
      } else {
        rightExpr = parseCondition();
      }

      auto combinedNode = std::make_unique<Node>();
      combinedNode->op = "OR";
      combinedNode->l = std::move(curNode);
      combinedNode->r = std::move(rightExpr);
      curNode = std::move(combinedNode);
    }

    return curNode;
  }

  /**
   * @brief Initiates parsing of the query structure.
   */
  ParsedQuery parseQuery() {
    std::string selectKeyword = toUppercase(eat());
    if (selectKeyword != "SELECT") {
      throw std::runtime_error("Query must start with SELECT");
    }

    std::string col = eat();

    std::string fromKeyword = toUppercase(eat());
    if (fromKeyword != "FROM") {
      throw std::runtime_error("Expected FROM keyword");
    }

    std::string table = eat();

    std::string whereKeyword = toUppercase(eat());
    if (whereKeyword != "WHERE") {
      throw std::runtime_error("Expected WHERE clause starting with WHERE");
    }

    return {col, table, parseExpression()};
  }
};

/**
 * @brief Evaluates an AST Node condition recursively against an Employee.
 *
 * @param node Current Node in the AST.
 * @param employee Employee record to evaluate.
 * @return true if matches, false otherwise.
 */
bool evaluate(const Node *node, const Employee &employee) {
  if (!node)
    return false;

  if (node->op == "OR") {
    return evaluate(node->l.get(), employee) ||
           evaluate(node->r.get(), employee);
  }

  int colValue = 0;
  if (node->col == "id") {
    colValue = employee.id;
  } else if (node->col == "age") {
    colValue = employee.age;
  } else {
    throw std::runtime_error("Unknown column in condition: '" + node->col +
                             "'");
  }

  if (node->op == ">")
    return colValue > node->val;
  if (node->op == "<")
    return colValue < node->val;
  if (node->op == ">=")
    return colValue >= node->val;
  if (node->op == "<=")
    return colValue <= node->val;
  if (node->op == "=")
    return colValue == node->val;

  throw std::runtime_error("Unsupported comparison operator: '" + node->op +
                           "'");
}

int main() {
  try {
    // std::vector<Employee> employees = {
    //     {"Abdullah Danish", 1, 19}, {"Riya", 2, 20},   {"Karan", 3, 19},
    //     {"Sneha", 4, 21},           {"Vivaan", 5, 20}, {"Ishaan", 6, 22}};

    std::vector<Employee> employees = {
        {"Bhavya Jain", 1, 19}, {"Bhavya", 2, 20},   {"Karan", 3, 19},
        {"Sneha", 4, 21},         {"Vivaan", 5, 20}, {"Ishaan", 6, 31}};

    std::string sqlQuery =
        "SELECT name FROM employees WHERE id >= 3 OR age < 20";

    auto tokens = tokenize(sqlQuery);
    Parser parser(tokens);
    auto query = parser.parseQuery();

    for (const auto &emp : employees) {
      if (!evaluate(query.whereRoot.get(), emp)) {
        continue;
      }
      if (query.selectedColumn == "name") {
        std::cout << emp.name << "\n";
      } else if (query.selectedColumn == "id") {
        std::cout << emp.id << "\n";
      } else if (query.selectedColumn == "age") {
        std::cout << emp.age << "\n";
      } else {
        std::cout << "Unknown column: " << query.selectedColumn << "\n";
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
