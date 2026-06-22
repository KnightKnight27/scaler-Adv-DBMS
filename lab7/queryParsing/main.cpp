#include <bits/stdc++.h>
using namespace std;

struct Emp 
{
    string name;
    int id, age;
};

struct Node 
{
    string op;
    string col;
    int val;
    Node *l = nullptr, *r = nullptr;
};

struct Tok 
{
    string s;
};

vector<Tok> lex(string q) 
{
    vector<Tok> t;

    for (int i = 0; i < (int)q.size();) 
    {

        if (isspace(q[i])) 
        {
            i++;
            continue;
        }

        if (isalpha(q[i])) 
        {
            string cur;

            while (i < (int)q.size() && (isalnum(q[i]) || q[i] == '_')) cur += q[i++];

            t.push_back({cur});
        }

        else if (isdigit(q[i])) 
        {
            string cur;

            while (i < (int)q.size() && isdigit(q[i])) cur += q[i++];

            t.push_back({cur});
        }

        else if ((q[i] == '>' || q[i] == '<') && i + 1 < (int)q.size() && q[i + 1] == '=') 
        {
            string cur;
            cur += q[i];
            cur += '=';
            t.push_back({cur});
            i += 2;
        }

        else 
        {
            t.push_back({string(1, q[i])});
            i++;
        }
    }

    return t;
}

struct Query 
{
    string col;
    string table;
    Node* root;
};

struct Parser 
{
    vector<Tok> t;
    int p = 0;

    Parser(vector<Tok> v) 
    {
        t = move(v);
    }

    string up(string s) 
    {
        for (char &c : s) c = toupper(c);
        return s;
    }

    string eat() 
    {
        return t[p++].s;
    }

    Node* cond() 
    {

        string c = eat();
        string op = eat();
        int val = stoi(eat());

        Node* x = new Node();

        x->op = op;
        x->col = c;
        x->val = val;

        return x;
    }

    Node* expr() 
    {

        Node* cur;

        if (t[p].s == "(") 
        {
            eat();
            cur = expr();
            eat();
        }
        else cur = cond();

        while (p < (int)t.size() && up(t[p].s) == "OR") 
        {

            eat();

            Node* rhs;

            if (t[p].s == "(") 
            {
                eat();
                rhs = expr();
                eat();
            }
            else rhs = cond();

            Node* par = new Node();
            par->op = "OR";
            par->l = cur;
            par->r = rhs;

            cur = par;
        }

        return cur;
    }

    Query build() 
    {

        eat();          // SELECT
        string col = eat();

        eat();          // FROM
        string tab = eat();

        eat();          // WHERE

        return {col, tab, expr()};
    }
};

bool eval(Node* cur, const Emp& e) 
{

    if (cur->op == "OR") return eval(cur->l, e) || eval(cur->r, e);

    int x = 0;

    if (cur->col == "id") x = e.id;
    else if (cur->col == "age") x = e.age;

    if (cur->op == ">") return x > cur->val;
    if (cur->op == "<") return x < cur->val;
    if (cur->op == ">=") return x >= cur->val;
    if (cur->op == "<=") return x <= cur->val;

    return x == cur->val;
}

int main() 
{

    vector<Emp> v = 
    {
        {"Pratham",1,19},
        {"Riya",2,20},
        {"Karan",3,19},
        {"Sneha",4,21},
        {"Vivaan",5,20},
        {"Ishaan",6,22}
    };

    string q = "SELECT name FROM employees WHERE id >= 3 OR age < 20";

    auto tok = lex(q);

    Parser P(tok);

    auto qry = P.build();

    for (auto &x : v) 
    {
        if (!eval(qry.root, x)) continue;
        if (qry.col == "name") cout << x.name << endl;
        else if (qry.col == "id") cout << x.id << endl;
        else cout << x.age << endl;
    }
}