#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>

using namespace std;

struct Token
{
  string text;
  bool isOperator;
};

// comparisons > AND > OR
int precedence(const string &op)
{
  if (op == ">" || op == "<" || op == ">=" || op == "<=" || op == "=")
    return 3;
  if (op == "AND")
    return 2;
  if (op == "OR")
    return 1;
  return 0;
}

bool isOperatorToken(const string &t)
{
  return precedence(t) > 0;
}

vector<Token> tokenize(const string &input)
{
  vector<Token> tokens;
  int pos = 0;
  while (pos < (int)input.size())
  {
    if (isspace(input[pos]))
    {
      ++pos;
      continue;
    }

    if (isalpha(input[pos]))
    {
      string word;
      while (pos < (int)input.size() &&
             (isalnum(input[pos]) || input[pos] == '_'))
      {
        word += input[pos++];
      }
      string upper;
      for (char c : word)
        upper += toupper(c);
      if (upper == "AND" || upper == "OR")
        tokens.push_back({upper, true});
      else
        tokens.push_back({word, false});
    }
    else if (isdigit(input[pos]))
    {
      string num;
      while (pos < (int)input.size() && isdigit(input[pos]))
        num += input[pos++];
      tokens.push_back({num, false});
    }
    else if ((input[pos] == '>' || input[pos] == '<') &&
             pos + 1 < (int)input.size() && input[pos + 1] == '=')
    {
      tokens.push_back({string() + input[pos] + '=', true});
      pos += 2;
    }
    else if (input[pos] == '>' || input[pos] == '<' || input[pos] == '=')
    {
      tokens.push_back({string(1, input[pos]), true});
      ++pos;
    }
    else if (input[pos] == '(')
    {
      tokens.push_back({"(", false});
      ++pos;
    }
    else if (input[pos] == ')')
    {
      tokens.push_back({")", false});
      ++pos;
    }
    else
    {
      ++pos;
    }
  }
  return tokens;
}

vector<Token> shuntingYard(const vector<Token> &tokens)
{
  vector<Token> output;
  stack<Token> operators;

  for (const Token &tok : tokens)
  {
    if (tok.text == "(")
    {
      operators.push(tok);
    }
    else if (tok.text == ")")
    {
      while (!operators.empty() && operators.top().text != "(")
      {
        output.push_back(operators.top());
        operators.pop();
      }
      if (!operators.empty())
        operators.pop();
    }
    else if (tok.isOperator)
    {
      while (!operators.empty() &&
             operators.top().text != "(" &&
             precedence(operators.top().text) >= precedence(tok.text))
      {
        output.push_back(operators.top());
        operators.pop();
      }
      operators.push(tok);
    }
    else
    {
      output.push_back(tok);
    }
  }

  while (!operators.empty())
  {
    output.push_back(operators.top());
    operators.pop();
  }
  return output;
}

struct Employee
{
  string name;
  int id;
  int age;
};

int columnValue(const string &name, const Employee &row)
{
  if (name == "id")
    return row.id;
  if (name == "age")
    return row.age;
  return 0;
}

bool isNumber(const string &s)
{
  for (char c : s)
    if (!isdigit(c))
      return false;
  return !s.empty();
}

bool evaluatePostfix(const vector<Token> &postfix, const Employee &row)
{
  stack<int> st;
  for (const Token &tok : postfix)
  {
    if (!tok.isOperator)
    {
      st.push(isNumber(tok.text) ? stoi(tok.text) : columnValue(tok.text, row));
      continue;
    }
    int b = st.top();
    st.pop();
    int a = st.top();
    st.pop();
    const string &op = tok.text;
    if (op == ">")
      st.push(a > b);
    else if (op == "<")
      st.push(a < b);
    else if (op == ">=")
      st.push(a >= b);
    else if (op == "<=")
      st.push(a <= b);
    else if (op == "=")
      st.push(a == b);
    else if (op == "AND")
      st.push(a && b);
    else if (op == "OR")
      st.push(a || b);
  }
  return st.top();
}

int main()
{
  string whereClause = "id > 3 AND (age < 25 OR age >= 30)";

  cout << "Infix WHERE:  " << whereClause << "\n";

  vector<Token> tokens = tokenize(whereClause);
  vector<Token> postfix = shuntingYard(tokens);

  cout << "Postfix (RPN): ";
  for (const Token &t : postfix)
    cout << t.text << ' ';
  cout << "\n\n";

  vector<Employee> employees = {
      {"Ankita", 1, 22},
      {"Manjari", 2, 22},
      {"Kartik", 3, 28},
      {"Vedika", 4, 24},
      {"Yashvi", 5, 22},
      {"Sunidhi", 6, 31},
  };

  cout << "Rows matching the WHERE clause:\n";
  for (const Employee &row : employees)
  {
    if (evaluatePostfix(postfix, row))
      cout << "  " << row.name << " (id=" << row.id << ", age=" << row.age << ")\n";
  }

  return 0;
}
