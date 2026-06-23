#include <iostream>
#include <vector>
#include <stack>
#include <string>
#include <cctype>
#include <algorithm>

using namespace std;

struct Employee
{
    string name;
    int id;
    int age;
};

bool isOperator(const string& token)
{
    return token == ">" || token == "<" ||
           token == ">=" || token == "<=" ||
           token == "=" || token == "AND" ||
           token == "OR";
}

int getPriority(const string& op)
{
    if(op == "AND") return 2;
    if(op == "OR") return 1;

    if(op == ">" || op == "<" ||
       op == ">=" || op == "<=" ||
       op == "=")
        return 3;

    return -1;
}

bool isNumeric(const string& token)
{
    for(char ch : token)
    {
        if(!isdigit(ch))
            return false;
    }
    return !token.empty();
}

vector<string> tokenize(const string& expression)
{
    vector<string> tokens;
    int n = expression.size();

    for(int i = 0; i < n;)
    {
        if(isspace(expression[i]))
        {
            i++;
            continue;
        }

        if(isalpha(expression[i]))
        {
            string word;

            while(i < n &&
                 (isalnum(expression[i]) ||
                  expression[i] == '_'))
            {
                word += expression[i++];
            }

            string upperWord = word;
            transform(
                upperWord.begin(),
                upperWord.end(),
                upperWord.begin(),
                ::toupper
            );

            if(upperWord == "AND" || upperWord == "OR")
                tokens.push_back(upperWord);
            else
                tokens.push_back(word);
        }
        else if(isdigit(expression[i]))
        {
            string number;

            while(i < n && isdigit(expression[i]))
            {
                number += expression[i++];
            }

            tokens.push_back(number);
        }
        else
        {
            if(i + 1 < n &&
               (expression[i] == '>' || expression[i] == '<') &&
               expression[i + 1] == '=')
            {
                tokens.push_back(
                    string() + expression[i] + '='
                );
                i += 2;
            }
            else
            {
                tokens.push_back(
                    string(1, expression[i])
                );
                i++;
            }
        }
    }

    return tokens;
}

vector<string> infixToPostfix(const vector<string>& tokens)
{
    vector<string> postfix;
    stack<string> operators;

    for(const string& token : tokens)
    {
        if(token == "(")
        {
            operators.push(token);
        }
        else if(token == ")")
        {
            while(!operators.empty() &&
                  operators.top() != "(")
            {
                postfix.push_back(operators.top());
                operators.pop();
            }

            if(!operators.empty())
                operators.pop();
        }
        else if(isOperator(token))
        {
            while(!operators.empty() &&
                  operators.top() != "(" &&
                  getPriority(operators.top()) >= getPriority(token))
            {
                postfix.push_back(operators.top());
                operators.pop();
            }

            operators.push(token);
        }
        else
        {
            postfix.push_back(token);
        }
    }

    while(!operators.empty())
    {
        postfix.push_back(operators.top());
        operators.pop();
    }

    return postfix;
}

int getFieldValue(const Employee& emp,
                  const string& field)
{
    if(field == "id")
        return emp.id;

    if(field == "age")
        return emp.age;

    return 0;
}

bool evaluatePostfix(const vector<string>& postfix,
                     const Employee& emp)
{
    stack<int> values;

    for(const string& token : postfix)
    {
        if(!isOperator(token))
        {
            if(isNumeric(token))
                values.push(stoi(token));
            else
                values.push(
                    getFieldValue(emp, token)
                );

            continue;
        }

        int right = values.top();
        values.pop();

        int left = values.top();
        values.pop();

        if(token == ">")
            values.push(left > right);
        else if(token == "<")
            values.push(left < right);
        else if(token == ">=")
            values.push(left >= right);
        else if(token == "<=")
            values.push(left <= right);
        else if(token == "=")
            values.push(left == right);
        else if(token == "AND")
            values.push(left && right);
        else
            values.push(left || right);
    }

    return values.top();
}

int main()
{
    string query =
        "id > 3 AND (age < 25 OR age >= 30)";

    vector<string> tokens =
        tokenize(query);

    vector<string> postfix =
        infixToPostfix(tokens);

    cout << "Postfix Expression:\n";

    for(const string& token : postfix)
    {
        cout << token << " ";
    }

    cout << "\n\n";

    vector<Employee> employees =
    {
        {"Pratham",1,19},
        {"Riya",2,20},
        {"Karan",3,19},
        {"Sneha",4,21},
        {"Vivaan",5,20},
        {"Ishaan",6,31},
        {"Meera",7,22}
    };

    cout << "Matching Employees:\n";

    for(const auto& emp : employees)
    {
        if(evaluatePostfix(postfix, emp))
        {
            cout << emp.name
                 << " "
                 << emp.id
                 << " "
                 << emp.age
                 << "\n";
        }
    }

    return 0;
}