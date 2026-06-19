#pragma once

#include <cctype>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

using namespace std;

struct Row {
  int id;
  string name;
  int age;
};

class ExpressionEvaluator {
private:
  int precedence(char op) {
    if (op == '+' || op == '-')
      return 1;
    if (op == '*' || op == '/')
      return 2;
    return 0;
  }

  int applyOp(int a, int b, char op) {
    switch (op) {
    case '+':
      return a + b;
    case '-':
      return a - b;
    case '*':
      return a * b;
    case '/':
      return a / b;
    }
    return 0;
  }

public:
  int evaluate(string expr) {
    stack<int> values;
    stack<char> ops;

    for (int i = 0; i < expr.length(); i++) {

      if (expr[i] == ' ')
        continue;

      if (isdigit(expr[i])) {
        int val = 0;

        while (i < expr.length() && isdigit(expr[i])) {
          val = val * 10 + (expr[i] - '0');
          i++;
        }

        values.push(val);
        i--;
      } else if (expr[i] == '(') {
        ops.push(expr[i]);
      } else if (expr[i] == ')') {

        while (!ops.empty() && ops.top() != '(') {

          int b = values.top();
          values.pop();
          int a = values.top();
          values.pop();

          char op = ops.top();
          ops.pop();

          values.push(applyOp(a, b, op));
        }

        ops.pop();
      } else {

        while (!ops.empty() && precedence(ops.top()) >= precedence(expr[i])) {

          int b = values.top();
          values.pop();
          int a = values.top();
          values.pop();

          char op = ops.top();
          ops.pop();

          values.push(applyOp(a, b, op));
        }

        ops.push(expr[i]);
      }
    }

    while (!ops.empty()) {

      int b = values.top();
      values.pop();
      int a = values.top();
      values.pop();

      char op = ops.top();
      ops.pop();

      values.push(applyOp(a, b, op));
    }

    return values.top();
  }
};

class SQLParser {
public:
  void executeQuery(const string &query, const vector<Row> &table) {

    string column;
    string conditionColumn;
    string conditionValue;

    stringstream ss(query);

    string temp;

    ss >> temp;
    ss >> column;
    ss >> temp;
    ss >> temp;

    bool hasWhere = false;

    if (ss >> temp) {

      if (temp == "WHERE") {

        hasWhere = true;

        ss >> conditionColumn;
        ss >> temp;
        ss >> conditionValue;
      }
    }

    cout << "\nQuery Result:\n";

    for (const auto &row : table) {

      bool match = true;

      if (hasWhere) {

        if (conditionColumn == "age")
          match = (row.age == stoi(conditionValue));

        else if (conditionColumn == "id")
          match = (row.id == stoi(conditionValue));

        else if (conditionColumn == "name")
          match = (row.name == conditionValue);
      }

      if (match) {

        if (column == "*")
          cout << row.id << " " << row.name << " " << row.age << endl;

        else if (column == "id")
          cout << row.id << endl;

        else if (column == "name")
          cout << row.name << endl;

        else if (column == "age")
          cout << row.age << endl;
      }
    }
  }
};