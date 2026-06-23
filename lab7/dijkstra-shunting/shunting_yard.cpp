#include <bits/stdc++.h>


using namespace std;

struct Employee
{
    string name;
    int id;
    int age;
};

bool isNumber(const string& s)
{
    return !s.empty() &&
           all_of(s.begin(), s.end(),
                 [](char c){ return isdigit(c); });
}

int precedence(const string& op)
{
    if(op==">" || op=="<" ||
       op==">=" || op=="<=" ||
       op=="=" || op=="!=")
        return 3;

    if(op=="AND")
        return 2;

    if(op=="OR")
        return 1;

    return 0;
}

vector<string> tokenize(const string& expr)
{
    vector<string> tokens;

    int i = 0;

    while(i < expr.size())
    {
        if(isspace(expr[i]))
        {
            i++;
            continue;
        }

        if(isalpha(expr[i]))
        {
            string word;

            while(i < expr.size() &&
                 (isalnum(expr[i]) || expr[i]=='_'))
            {
                word += expr[i++];
            }

            string upper = word;

            for(char& c : upper)
                c = toupper(c);

            if(upper=="AND" || upper=="OR")
                tokens.push_back(upper);
            else
                tokens.push_back(word);
        }
        else if(isdigit(expr[i]))
        {
            string num;

            while(i < expr.size() &&
                  isdigit(expr[i]))
            {
                num += expr[i++];
            }

            tokens.push_back(num);
        }
        else if(i+1 < expr.size())
        {
            string two = expr.substr(i,2);

            if(two==">=" || two=="<=" || two=="!=")
            {
                tokens.push_back(two);
                i += 2;
                continue;
            }

            if(expr[i]=='=')
            {
                tokens.push_back("=");
                i++;
                continue;
            }

            tokens.push_back(string(1,expr[i]));
            i++;
        }
        else
        {
            tokens.push_back(string(1,expr[i]));
            i++;
        }
    }

    return tokens;
}

vector<string> infixToPostfix(const vector<string>& tokens)
{
    vector<string> output;
    stack<string> operators;

    for(const auto& token : tokens)
    {
        if(token=="(")
        {
            operators.push(token);
        }
        else if(token==")")
        {
            while(!operators.empty() &&
                  operators.top()!="(")
            {
                output.push_back(operators.top());
                operators.pop();
            }

            if(!operators.empty())
                operators.pop();
        }
        else if(precedence(token))
        {
            while(!operators.empty() &&
                  operators.top()!="(" &&
                  precedence(operators.top()) >= precedence(token))
            {
                output.push_back(operators.top());
                operators.pop();
            }

            operators.push(token);
        }
        else
        {
            output.push_back(token);
        }
    }

    while(!operators.empty())
    {
        output.push_back(operators.top());
        operators.pop();
    }

    return output;
}

int getColumnValue(const string& column,
                   const Employee& emp)
{
    if(column=="id")
        return emp.id;

    if(column=="age")
        return emp.age;

    return 0;
}

bool evaluatePostfix(const vector<string>& postfix,
                     const Employee& emp)
{
    stack<int> values;

    for(const auto& token : postfix)
    {
        if(!precedence(token))
        {
            if(isNumber(token))
                values.push(stoi(token));
            else
                values.push(getColumnValue(token,emp));

            continue;
        }

        int right = values.top();
        values.pop();

        int left = values.top();
        values.pop();

        if(token==">")
            values.push(left > right);

        else if(token=="<")
            values.push(left < right);

        else if(token==">=")
            values.push(left >= right);

        else if(token=="<=")
            values.push(left <= right);

        else if(token=="=")
            values.push(left == right);

        else if(token=="!=")
            values.push(left != right);

        else if(token=="AND")
            values.push(left && right);

        else if(token=="OR")
            values.push(left || right);
    }

    return values.top();
}

int main()
{
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

    string condition =
        "id > 3 AND ( age < 25 OR age >= 30 )";

    auto tokens = tokenize(condition);
    auto postfix = infixToPostfix(tokens);

    cout << "Postfix Expression:\n";

    for(const auto& x : postfix)
        cout << x << " ";

    cout << "\n\nMatching Employees:\n";

    for(const auto& emp : employees)
    {
        if(evaluatePostfix(postfix,emp))
        {
            cout << emp.name << " "
                 << emp.id << " "
                 << emp.age << '\n';
        }
    }

    return 0;
}