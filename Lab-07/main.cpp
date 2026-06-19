#include "expression_sql.hpp"

using namespace std;

// ADBMS LAB 5
// Roll No: 24BCS10199
// Name: Ayushkumar Singh

int main() {

  ExpressionEvaluator evaluator;

  string expression = "(10+5)*2-8/2";

  cout << "Expression: " << expression << endl;

  cout << "Result: " << evaluator.evaluate(expression) << endl;

  vector<Row> students = {
      {1, "Ayush", 19}, {2, "Anushka", 20}, {3, "Rahul", 19}, {4, "Priya", 21}};

  SQLParser parser;

  parser.executeQuery("SELECT name FROM table WHERE age = 19", students);

  return 0;
}