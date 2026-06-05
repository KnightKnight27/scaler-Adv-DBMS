#include <cctype>
#include <iostream>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

enum class TokenType {
  KEYWORD,
  IDENTIFIER,
  OPERATOR,
  NUMBER,
  STRING,
  LPAREN,
  RPAREN,
  COMMA,
  UNKNOWN
};

struct Token {
  TokenType type;
  string value;
};

struct Expression {
  vector<Token> postfix;
};

struct SQLQuery {
  vector<string> columns;
  string tableName;
  Expression whereClause;
};

vector<Token> tokenize(const string &query) {
  vector<Token> tokens;
  int n = query.length();
  int i = 0;

  auto isOperatorChar = [](char c) {
    return c == '=' || c == '<' || c == '>' || c == '!';
  };

  while (i < n) {
    if (isspace(query[i])) {
      i++;
      continue;
    }

    if (query[i] == '(') {
      tokens.push_back({TokenType::LPAREN, "("});
      i++;
    } else if (query[i] == ')') {
      tokens.push_back({TokenType::RPAREN, ")"});
      i++;
    } else if (query[i] == ',') {
      tokens.push_back({TokenType::COMMA, ","});
      i++;
    } else if (query[i] == ';') {
      i++;
    } else if (query[i] == '\'') {
      string val = "";
      i++;
      while (i < n && query[i] != '\'') {
        val += query[i];
        i++;
      }
      if (i < n)
        i++;
      tokens.push_back({TokenType::STRING, "'" + val + "'"});
    } else if (isdigit(query[i])) {
      string val = "";
      while (i < n && isdigit(query[i])) {
        val += query[i];
        i++;
      }
      tokens.push_back({TokenType::NUMBER, val});
    } else if (isOperatorChar(query[i])) {
      string val = "";
      val += query[i];
      i++;
      if (i < n && query[i] == '=') {
        val += query[i];
        i++;
      }
      tokens.push_back({TokenType::OPERATOR, val});
    } else if (isalpha(query[i]) || query[i] == '_' || query[i] == '*') {
      string val = "";
      while (i < n &&
             (isalnum(query[i]) || query[i] == '_' || query[i] == '*')) {
        val += query[i];
        i++;
      }

      string upperVal = val;
      for (char &c : upperVal)
        c = toupper(c);

      if (upperVal == "AND" || upperVal == "OR") {
        tokens.push_back({TokenType::OPERATOR, upperVal});
      } else if (upperVal == "SELECT" || upperVal == "FROM" ||
                 upperVal == "WHERE") {
        tokens.push_back({TokenType::KEYWORD, upperVal});
      } else {
        tokens.push_back({TokenType::IDENTIFIER, val});
      }
    } else {
      i++;
    }
  }
  return tokens;
}

int getPrecedence(const string &op) {
  if (op == "OR")
    return 1;
  if (op == "AND")
    return 2;
  if (op == "=" || op == "!=" || op == ">" || op == "<" || op == ">=" ||
      op == "<=")
    return 3;
  return 0;
}

Expression parseWhereClause(const vector<Token> &tokens) {
  Expression expr;
  stack<Token> opStack;

  for (const auto &token : tokens) {
    if (token.type == TokenType::IDENTIFIER ||
        token.type == TokenType::NUMBER || token.type == TokenType::STRING) {
      expr.postfix.push_back(token);
    } else if (token.type == TokenType::LPAREN) {
      opStack.push(token);
    } else if (token.type == TokenType::RPAREN) {
      while (!opStack.empty() && opStack.top().type != TokenType::LPAREN) {
        expr.postfix.push_back(opStack.top());
        opStack.pop();
      }
      if (!opStack.empty())
        opStack.pop();
    } else if (token.type == TokenType::OPERATOR) {
      while (!opStack.empty() && opStack.top().type != TokenType::LPAREN &&
             getPrecedence(opStack.top().value) >= getPrecedence(token.value)) {
        expr.postfix.push_back(opStack.top());
        opStack.pop();
      }
      opStack.push(token);
    }
  }

  while (!opStack.empty()) {
    expr.postfix.push_back(opStack.top());
    opStack.pop();
  }

  return expr;
}

SQLQuery parseQuery(const string &queryString) {
  SQLQuery query;
  vector<Token> tokens = tokenize(queryString);

  if (tokens.empty())
    return query;

  int i = 0;
  if (i < tokens.size() && tokens[i].type == TokenType::KEYWORD &&
      tokens[i].value == "SELECT") {
    i++;
  }

  while (i < tokens.size() &&
         !(tokens[i].type == TokenType::KEYWORD && tokens[i].value == "FROM")) {
    if (tokens[i].type == TokenType::IDENTIFIER ||
        (tokens[i].type == TokenType::IDENTIFIER && tokens[i].value == "*")) {
      query.columns.push_back(tokens[i].value);
    }
    i++;
  }

  if (i < tokens.size() && tokens[i].type == TokenType::KEYWORD &&
      tokens[i].value == "FROM") {
    i++;
  }

  if (i < tokens.size() && tokens[i].type == TokenType::IDENTIFIER) {
    query.tableName = tokens[i].value;
    i++;
  }

  if (i < tokens.size() && tokens[i].type == TokenType::KEYWORD &&
      tokens[i].value == "WHERE") {
    i++;
    vector<Token> whereTokens;
    while (i < tokens.size()) {
      whereTokens.push_back(tokens[i]);
      i++;
    }
    query.whereClause = parseWhereClause(whereTokens);
  }

  return query;
}

string tokenTypeToString(TokenType type) {
  switch (type) {
  case TokenType::KEYWORD:
    return "KEYWORD";
  case TokenType::IDENTIFIER:
    return "IDENTIFIER";
  case TokenType::OPERATOR:
    return "OPERATOR";
  case TokenType::NUMBER:
    return "NUMBER";
  case TokenType::STRING:
    return "STRING";
  case TokenType::LPAREN:
    return "(";
  case TokenType::RPAREN:
    return ")";
  case TokenType::COMMA:
    return ",";
  default:
    return "UNKNOWN";
  }
}

void printQuery(const SQLQuery &q) {
  cout << "Table: " << (q.tableName.empty() ? "<none>" : q.tableName) << "\n";
  cout << "Columns: ";
  if (q.columns.empty())
    cout << "<none>";
  for (const auto &col : q.columns)
    cout << col << " ";
  cout << "\nWhere (Postfix representation): ";
  if (q.whereClause.postfix.empty())
    cout << "<none>";
  for (const auto &tok : q.whereClause.postfix)
    cout << "[" << tok.value << "] ";
  cout << "\n\n";
}

int main() {
  cout << "--- Minimal SQL Query Parser ---\n\n";

  vector<string> testQueries = {
      "SELECT A, B FROM my_table WHERE A > 1000 AND B < 1000;",
      "SELECT * FROM users WHERE (age >= 18 OR status = 'active') AND points > "
      "50;",
      "SELECT id, name, email FROM students WHERE grade = 'A';",
      "SELECT id, name, sport1, runs_scored FROM athletes WHERE (sport1 = "
      "'cricket' AND runs_scored > 1000);"};

  for (const auto &q : testQueries) {
    cout << "Parsing: " << q << "\n";
    SQLQuery parsed = parseQuery(q);
    printQuery(parsed);
  }

  return 0;
}
