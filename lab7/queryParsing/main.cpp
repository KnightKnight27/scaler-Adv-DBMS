#include <iostream>
#include <vector>
#include <string>
#include <cctype>

using namespace std;

struct Employee
{
    string name;
    int id;
    int age;
};

struct Token
{
    string text;
};

struct ASTNode
{
    string operation;
    string columnName;
    int compareValue = 0;

    ASTNode* leftChild = nullptr;
    ASTNode* rightChild = nullptr;
};

struct QueryInfo
{
    string selectedColumn;
    string tableName;
    ASTNode* whereClause;
};

vector<Token> tokenizeQuery(const string& query)
{
    vector<Token> tokens;

    int index = 0;

    while(index < (int)query.size())
    {
        if(isspace(query[index]))
        {
            index++;
            continue;
        }

        if(isalpha(query[index]))
        {
            string word;

            while(index < (int)query.size() &&
                 (isalnum(query[index]) ||
                  query[index] == '_'))
            {
                word += query[index++];
            }

            tokens.push_back({word});
        }
        else if(isdigit(query[index]))
        {
            string number;

            while(index < (int)query.size() &&
                  isdigit(query[index]))
            {
                number += query[index++];
            }

            tokens.push_back({number});
        }
        else if((query[index] == '>' ||
                 query[index] == '<') &&
                 index + 1 < (int)query.size() &&
                 query[index + 1] == '=')
        {
            string op;
            op += query[index];
            op += '=';

            tokens.push_back({op});
            index += 2;
        }
        else
        {
            tokens.push_back(
                {string(1, query[index])}
            );
            index++;
        }
    }

    return tokens;
}

class QueryParser
{
private:

    vector<Token> tokens;
    int current = 0;

    string toUpperCase(string str)
    {
        for(char& ch : str)
            ch = toupper(ch);

        return str;
    }

    string consume()
    {
        return tokens[current++].text;
    }

    string currentToken()
    {
        return tokens[current].text;
    }

    ASTNode* parsePredicate()
    {
        ASTNode* node = new ASTNode();

        node->columnName = consume();
        node->operation = consume();
        node->compareValue = stoi(consume());

        return node;
    }

    ASTNode* parseFactor()
    {
        if(currentToken() == "(")
        {
            consume();

            ASTNode* result = parseExpression();

            consume(); // )

            return result;
        }

        return parsePredicate();
    }

    ASTNode* parseExpression()
    {
        ASTNode* root = parseFactor();

        while(current < (int)tokens.size() &&
              toUpperCase(currentToken()) == "OR")
        {
            consume();

            ASTNode* rightSide = parseFactor();

            ASTNode* parent = new ASTNode();
            parent->operation = "OR";

            parent->leftChild = root;
            parent->rightChild = rightSide;

            root = parent;
        }

        return root;
    }

public:

    QueryParser(vector<Token> tokenList)
        : tokens(move(tokenList))
    {
    }

    QueryInfo parse()
    {
        consume(); // SELECT

        string column = consume();

        consume(); // FROM

        string table = consume();

        consume(); // WHERE

        ASTNode* whereRoot = parseExpression();

        return {column, table, whereRoot};
    }
};

int fetchFieldValue(const Employee& emp,
                    const string& field)
{
    if(field == "id")
        return emp.id;

    if(field == "age")
        return emp.age;

    return 0;
}

bool evaluateCondition(ASTNode* node,
                       const Employee& emp)
{
    if(node->operation == "OR")
    {
        return evaluateCondition(node->leftChild, emp) ||
               evaluateCondition(node->rightChild, emp);
    }

    int actualValue =
        fetchFieldValue(emp, node->columnName);

    if(node->operation == ">")
        return actualValue > node->compareValue;

    if(node->operation == "<")
        return actualValue < node->compareValue;

    if(node->operation == ">=")
        return actualValue >= node->compareValue;

    if(node->operation == "<=")
        return actualValue <= node->compareValue;

    return actualValue == node->compareValue;
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
        {"Ishaan",6,22}
    };

    string query =
        "SELECT name FROM employees WHERE id >= 3 OR age < 20";

    vector<Token> tokenStream =
        tokenizeQuery(query);

    QueryParser parser(tokenStream);

    QueryInfo parsedQuery =
        parser.parse();

    for(const auto& employee : employees)
    {
        if(!evaluateCondition(parsedQuery.whereClause,
                              employee))
        {
            continue;
        }

        if(parsedQuery.selectedColumn == "name")
            cout << employee.name << '\n';
        else if(parsedQuery.selectedColumn == "id")
            cout << employee.id << '\n';
        else
            cout << employee.age << '\n';
    }

    return 0;
}