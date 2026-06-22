#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <stack>
#include <algorithm>
#include <cctype>

using namespace std;

// Represent a row in a database table as key-value pairs
typedef unordered_map<string, string> Row;
typedef vector<Row> Table;

// Structure to represent parsed tokens
struct Token {
    enum Type { OPERAND, OPERATOR, LPAREN, RPAREN } type;
    string val;
};

// Operator Precedence helper
int getPrecedence(const string& op) {
    if (op == "OR") return 1;
    if (op == "AND") return 2;
    if (op == "==" || op == "!=" || op == ">" || op == "<" || op == ">=" || op == "<=")
    return 3;
    return 0;
}

// Tokenize a string expression
vector<Token> tokenize(const string& expr) {
    vector<Token> tokens;
    int i = 0;
    int n = expr.length();

    while (i < n) {
        if (isspace(expr[i])) {
            i++;
            continue;
        }

        // Parentheses
        if (expr[i] == '(') {
            tokens.push_back({Token::LPAREN, "("});
            i++;
        } else if (expr[i] == ')') {
            tokens.push_back({Token::RPAREN, ")"});
            i++;
        }
        // Operators (comparison/logical)
        else if (expr[i] == '=' && i + 1 < n && expr[i + 1] == '=') {
            tokens.push_back({Token::OPERATOR, "=="});
            i += 2;
        } else if (expr[i] == '!' && i + 1 < n && expr[i + 1] == '=') {
            tokens.push_back({Token::OPERATOR, "!="});
            i += 2;
        } else if (expr[i] == '>' && i + 1 < n && expr[i + 1] == '=') {
            tokens.push_back({Token::OPERATOR, ">="});
            i += 2;
        } else if (expr[i] == '<' && i + 1 < n && expr[i + 1] == '=') {
            tokens.push_back({Token::OPERATOR, "<="});
            i += 2;
        } else if (expr[i] == '>') {
            tokens.push_back({Token::OPERATOR, ">"});
            i++;
        } else if (expr[i] == '<') {
            tokens.push_back({Token::OPERATOR, "<"});
            i++;
        }
        // String Literals starting with single quotes
        else if (expr[i] == '\'') {
            string lit = "";
            i++; // skip open quote
            while (i < n && expr[i] != '\'') {
                lit += expr[i];
                i++;
            }
            if (i < n) i++; // skip close quote
            tokens.push_back({Token::OPERAND, lit});
        }
        // Identifiers, numbers, or logical operators (AND, OR)
        else {
            string current = "";
            while (i < n && !isspace(expr[i]) && expr[i] != '(' && expr[i] != ')' &&
                   expr[i] != '=' && expr[i] != '!' && expr[i] != '>' &&
                   expr[i] != '<' && expr[i] != '\'') {
                current += expr[i];
                i++;
            }

            if (current == "AND" || current == "OR") {
                tokens.push_back({Token::OPERATOR, current});
            } else {
                tokens.push_back({Token::OPERAND, current});
            }
        }
    }
    return tokens;
}

// Dijkstra's Shunting-Yard Algorithm to convert Infix to Postfix (RPN)
vector<Token> shuntingYard(const vector<Token>& infix) {
    vector<Token> postfix;
    stack<Token> opStack;

    for (const auto& token : infix) {
        if (token.type == Token::OPERAND) {
            postfix.push_back(token);
        } else if (token.type == Token::LPAREN) {
            opStack.push(token);
        } else if (token.type == Token::RPAREN) {
            while (!opStack.empty() && opStack.top().type != Token::LPAREN) {
                postfix.push_back(opStack.top());
                opStack.pop();
            }
            if (!opStack.empty()) opStack.pop(); // Pop LPAREN
        } else if (token.type == Token::OPERATOR) {
            while (!opStack.empty() && opStack.top().type == Token::OPERATOR &&
                   getPrecedence(opStack.top().val) >= getPrecedence(token.val)) {
                postfix.push_back(opStack.top());
                opStack.pop();
            }
            opStack.push(token);
        }
    }

    while (!opStack.empty()) {
        postfix.push_back(opStack.top());
        opStack.pop();
    }

    return postfix;
}

// Helper to determine if a string represents a number
bool isNumber(const string& s) {
    if (s.empty()) return false;
    for (char const &c : s) {
        if (std::isdigit(c) == 0 && c != '.' && c != '-') return false;
    }
    return true;
}

// Evaluate boolean comparison between two operands
bool evaluateComparison(const string& left, const string& op, const string& right) {
    if (isNumber(left) && isNumber(right)) {
        double lNum = stod(left);
        double rNum = stod(right);
        if (op == "==") return lNum == rNum;
        if (op == "!=") return lNum != rNum;
        if (op == ">") return lNum > rNum;
        if (op == "<") return lNum < rNum;
        if (op == ">=") return lNum >= rNum;
        if (op == "<=") return lNum <= rNum;
    } else {
        // String lexicographical comparison
        if (op == "==") return left == right;
        if (op == "!=") return left != right;
        if (op == ">") return left > right;
        if (op == "<") return left < right;
        if (op == ">=") return left >= right;
        if (op == "<=") return left <= right;
    }
    return false;
}

// Evaluate RPN expression against a single row
bool evaluateRPN(const vector<Token>& postfix, const Row& row) {
    stack<string> valStack;

    for (const auto& token : postfix) {
        if (token.type == Token::OPERAND) {
            // If the operand corresponds to a column name, substitute with the row value
            if (row.find(token.val) != row.end()) {
                valStack.push(row.at(token.val));
            } else {
                valStack.push(token.val);
            }
        } else if (token.type == Token::OPERATOR) {
            if (token.val == "AND" || token.val == "OR") {
                if (valStack.size() < 2) return false;
                string rightVal = valStack.top(); valStack.pop();
                string leftVal = valStack.top(); valStack.pop();

                bool leftBool = (leftVal == "1" || leftVal == "true");
                bool rightBool = (rightVal == "1" || rightVal == "true");

                bool result = false;
                if (token.val == "AND") result = leftBool && rightBool;
                if (token.val == "OR") result = leftBool || rightBool;

                valStack.push(result ? "true" : "false");
            } else {
                if (valStack.size() < 2) return false;
                string rightVal = valStack.top(); valStack.pop();
                string leftVal = valStack.top(); valStack.pop();

                bool compResult = evaluateComparison(leftVal, token.val, rightVal);
                valStack.push(compResult ? "true" : "false");
            }
        }
    }

    if (valStack.empty()) return false;
    return (valStack.top() == "true" || valStack.top() == "1");
}

// A simple SQL query string parser and executor
void executeSelect(const string& sqlQuery, const Table& table) {
    cout << "\n--------------------------------------------------\n";
    cout << "Executing Query: " << sqlQuery << "\n";
    cout << "--------------------------------------------------\n";

    // Basic parse logic: SELECT col1, col2 FROM tableName WHERE condition
    stringstream ss(sqlQuery);
    string temp;
    ss >> temp; // SELECT

    vector<string> selectColumns;
    string col;
    while (ss >> col && col != "FROM") {
        if (col.back() == ',') {
            col.pop_back(); // Remove comma
        }
        selectColumns.push_back(col);
    }

    string tableName;
    ss >> tableName;

    string whereClauseWord;
    ss >> whereClauseWord; // WHERE

    string condition = "";
    string word;
    while (ss >> word) {
        condition += word + " ";
    }
    if (!condition.empty()) {
        condition.pop_back(); // Remove trailing space
    }

    cout << "Parsed Metadata:\n";
    cout << " - SELECT Columns: ";
    for (const auto& c : selectColumns) cout << "[" << c << "] ";
    cout << "\n - From Table: " << tableName << "\n";
    cout << " - WHERE Condition: " << condition << "\n\n";

    // Compile the WHERE condition using Shunting-Yard
    vector<Token> tokens = tokenize(condition);
    vector<Token> postfix = shuntingYard(tokens);

    // Filter and print rows
    int matchCount = 0;
    // Header line
    for (const auto& colName : selectColumns) {
        cout << colName << "\t\t";
    }
    cout << "\n--------------------------------------------------\n";

    for (const auto& row : table) {
        if (evaluateRPN(postfix, row)) {
            matchCount++;
            for (const auto& colName : selectColumns) {
                if (row.find(colName) != row.end()) {
                    cout << row.at(colName) << "\t\t";
                } else {
                    cout << "NULL\t\t";
                }
            }
            cout << "\n";
        }
    }
    cout << "--------------------------------------------------\n";
    cout << "Query execution completed. Rows matched: " << matchCount << "\n\n";
}

int main() {
    // Populate sample table data
    Table users = {
        {{"id", "1"}, {"name", "Mehul"}, {"age", "21"}, {"city", "Mumbai"}},
        {{"id", "2"}, {"name", "Amit"}, {"age", "32"}, {"city", "Delhi"}},
        {{"id", "3"}, {"name", "Rahul"}, {"age", "45"}, {"city", "Mumbai"}},
        {{"id", "4"}, {"name", "Priya"}, {"age", "28"}, {"city", "Bangalore"}},
        {{"id", "5"}, {"name", "Sneha"}, {"age", "35"}, {"city", "Chennai"}},
        {{"id", "6"}, {"name", "Karan"}, {"age", "31"}, {"city", "Mumbai"}}
    };

    // Test Queries
    executeSelect("SELECT name, city FROM users WHERE city == 'Mumbai'", users);
    executeSelect("SELECT id, name, age FROM users WHERE age > 30", users);
    executeSelect("SELECT name, age, city FROM users WHERE city == 'Mumbai' AND age > 30",
    users);
    executeSelect("SELECT name, age FROM users WHERE city == 'Delhi' OR age <= 28", users);

    return 0;
}
