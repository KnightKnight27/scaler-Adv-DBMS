#include <bits/stdc++.h>

using namespace std;

struct Employee
{
    string name;
    int id;
    int age;
};

struct Query
{
    string selectedColumn;
    string tableName;
    string whereClause;
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

int getValue(const string& col,
             const Employee& emp)
{
    if(col=="id")
        return emp.id;

    if(col=="age")
        return emp.age;

    return 0;
}

bool evaluatePostfix(const vector<string>& postfix,
                     const Employee& emp)
{
    stack<int> st;

    for(const auto& token : postfix)
    {
        if(!precedence(token))
        {
            if(isNumber(token))
                st.push(stoi(token));
            else
                st.push(getValue(token,emp));

            continue;
        }

        int b = st.top();
        st.pop();

        int a = st.top();
        st.pop();

        if(token==">")
            st.push(a>b);

        else if(token=="<")
            st.push(a<b);

        else if(token==">=")
            st.push(a>=b);

        else if(token=="<=")
            st.push(a<=b);

        else if(token=="=")
            st.push(a==b);

        else if(token=="!=")
            st.push(a!=b);

        else if(token=="AND")
            st.push(a&&b);

        else if(token=="OR")
            st.push(a||b);
    }

    return st.top();
}

Query parseQuery(const string& sql)
{
    stringstream ss(sql);

    string selectWord;
    string column;
    string fromWord;
    string table;

    ss >> selectWord;
    ss >> column;
    ss >> fromWord;
    ss >> table;

    string whereKeyword;
    string whereClause;

    if(ss >> whereKeyword)
    {
        string token;

        while(ss >> token)
        {
            if(!whereClause.empty())
                whereClause += " ";

            whereClause += token;
        }
    }

    return {column, table, whereClause};
}

void executeQuery(const Query& query,
                  const vector<Employee>& employees)
{
    auto tokens = tokenize(query.whereClause);
    auto postfix = infixToPostfix(tokens);

    cout << "\nQuery Result:\n";

    for(const auto& emp : employees)
    {
        if(evaluatePostfix(postfix,emp))
        {
            if(query.selectedColumn=="name")
                cout << emp.name << '\n';

            else if(query.selectedColumn=="id")
                cout << emp.id << '\n';

            else if(query.selectedColumn=="age")
                cout << emp.age << '\n';
        }
    }
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

    string sql =
        "SELECT name FROM employees "
        "WHERE id > 3 AND ( age < 25 OR age >= 30 )";

    Query query = parseQuery(sql);

    executeQuery(query,employees);

    return 0;
}