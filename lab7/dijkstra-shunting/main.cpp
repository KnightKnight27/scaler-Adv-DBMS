#include <bits/stdc++.h>
using namespace std;

struct Emp
{
    string n;
    int id, age;
};

int prec(string s)
{
    if (s == ">" || s == "<" || s == ">=" || s == "<=" || s == "=") return 3;
    if (s == "AND") return 2;
    if (s == "OR") return 1;
    return 0;
}

bool isNum(string s)
{
    return !s.empty() && all_of(s.begin(), s.end(), [](char c) { return isdigit(c); });
}

vector<string> parse(string s)
{
    vector<string> v;

    for (int i = 0; i < (int)s.size();)
    {
        if (isspace(s[i]))
        {
            i++;
            continue;
        }

        if (isalpha(s[i]))
        {
            string cur;

            while (i < (int)s.size() && (isalnum(s[i]) || s[i] == '_')) cur += s[i++];

            string up = cur;
            for (char &c : up) c = toupper(c);

            if (up == "AND" || up == "OR") v.push_back(up);
            else v.push_back(cur);
        }
        else if (isdigit(s[i]))
        {
            string cur;

            while (i < (int)s.size() && isdigit(s[i])) cur += s[i++];

            v.push_back(cur);
        }
        else if ((s[i] == '>' || s[i] == '<') && i + 1 < (int)s.size() && s[i + 1] == '=')
        {

            string cur;
            cur += s[i];
            cur += '=';
            v.push_back(cur);
            i += 2;
        }
        else
        {
            v.push_back(string(1, s[i]));
            i++;
        }
    }

    return v;
}

vector<string> toPostfix(vector<string> &tok)
{
    vector<string> out;
    stack<string> st;

    for (auto &x : tok)
    {
        if (x == "(") st.push(x);

        else if (x == ")")
        {
            while (!st.empty() && st.top() != "(")
            {
                out.push_back(st.top());
                st.pop();
            }
            if (!st.empty()) st.pop();
        }

        else if (prec(x))
        {
            while (!st.empty() && st.top() != "(" && prec(st.top()) >= prec(x))
            {
                out.push_back(st.top());
                st.pop();
            }
            st.push(x);
        }

        else out.push_back(x);
    }

    while (!st.empty())
    {
        out.push_back(st.top());
        st.pop();
    }

    return out;
}

int getVal(string col, Emp e)
{
    if (col == "id") return e.id;
    if (col == "age") return e.age;
    return 0;
}

bool eval(vector<string> &pf, Emp e)
{
    stack<int> st;

    for (auto &x : pf)
    {
        if (!prec(x))
        {
            if (isNum(x)) st.push(stoi(x));
            else st.push(getVal(x, e));
            continue;
        }

        int b = st.top(); st.pop();
        int a = st.top(); st.pop();

        if (x == ">") st.push(a > b);
        else if (x == "<") st.push(a < b);
        else if (x == ">=") st.push(a >= b);
        else if (x == "<=") st.push(a <= b);
        else if (x == "=") st.push(a == b);
        else if (x == "AND") st.push(a && b);
        else st.push(a || b);
    }

    return st.top();
}

int main()
{
    string q = "id > 3 AND (age < 25 OR age >= 30)";

    auto tok = parse(q);
    auto pf = toPostfix(tok);

    cout << "Postfix: ";
    for (auto &x : pf) cout << x << ' ';
    cout << endl;
    cout << endl;


    vector<Emp> v = {
        {"Rohan",1,19},
        {"Riya",2,20},
        {"Karan",3,19},
        {"Sneha",4,21},
        {"Vivaan",5,20},
        {"Ishaan",6,31},
        {"Meera",7,22}
    };

    for (auto &e : v)
    {
        if (eval(pf, e)) cout << e.n << " " << e.id << " " << e.age << endl;
    }
}
