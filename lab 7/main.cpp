#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <cctype>
#include <algorithm>
#include <stdexcept>

using namespace std;

// Structure to hold student data
struct Student {
    int id;
    string name;
    int age;
    int marks;
};

// Types of tokens we can parse
enum class TokenType {
    Keyword,     // AND, OR
    Identifier,  // Column names (id, age, marks)
    Number,      // Numeric constants
    Operator,    // >, <, >=, <=, =, !=
    LParen,      // (
    RParen,      // )
    Invalid
};

struct Token {
    TokenType type;
    string text;
};

// Helper to convert strings to uppercase for case-insensitive SQL keywords
string toUpperCase(const string& str) {
    string upper = str;
    transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return ::toupper(c); // using standard library toupper
    });
    return upper;
}

// Tokenize the input WHERE clause string
vector<Token> tokenize(const string& expr) {
    vector<Token> tokens;
    size_t i = 0;

    while (i < expr.size()) {
        char c = expr[i];

        // Skip whitespace
        if (isspace(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }

        // Handle parentheses
        if (c == '(') {
            tokens.push_back({TokenType::LParen, "("});
            i++;
            continue;
        }
        if (c == ')') {
            tokens.push_back({TokenType::RParen, ")"});
            i++;
            continue;
        }

        // Handle comparison operators (two characters or one character)
        if (i + 1 < expr.size()) {
            string op2 = expr.substr(i, 2);
            if (op2 == ">=" || op2 == "<=" || op2 == "!=") {
                tokens.push_back({TokenType::Operator, op2});
                i += 2;
                continue;
            }
        }
        if (c == '>' || c == '<' || c == '=') {
            tokens.push_back({TokenType::Operator, string(1, c)});
            i++;
            continue;
        }

        // Handle numeric literals
        if (isdigit(static_cast<unsigned char>(c))) {
            string num = "";
            while (i < expr.size() && isdigit(static_cast<unsigned char>(expr[i]))) {
                num += expr[i];
                i++;
            }
            tokens.push_back({TokenType::Number, num});
            continue;
        }

        // Handle words (column identifiers or keywords like AND/OR)
        if (isalpha(static_cast<unsigned char>(c)) || c == '_') {
            string word = "";
            while (i < expr.size() && (isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) {
                word += expr[i];
                i++;
            }

            string upperWord = toUpperCase(word);
            if (upperWord == "AND" || upperWord == "OR") {
                tokens.push_back({TokenType::Keyword, upperWord});
            } else {
                tokens.push_back({TokenType::Identifier, word});
            }
            continue;
        }

        // Catch-all for unrecognized characters
        throw runtime_error("Lexical error: Unexpected character '" + string(1, c) + "' at position " + to_string(i));
    }

    return tokens;
}

// Get operator precedence (higher number = evaluated first)
int getPrecedence(const string& op) {
    if (op == ">" || op == "<" || op == ">=" || op == "<=" || op == "=" || op == "!=") {
        return 3;
    }
    if (op == "AND") return 2;
    if (op == "OR")  return 1;
    return 0;
}

// Convert Infix tokens to Postfix (Reverse Polish Notation) using Shunting-Yard
vector<Token> shuntingYard(const vector<Token>& tokens) {
    vector<Token> output;
    stack<Token> opStack;

    for (const auto& tok : tokens) {
        if (tok.type == TokenType::Number || tok.type == TokenType::Identifier) {
            // Operands go straight to the output queue
            output.push_back(tok);
        }
        else if (tok.type == TokenType::LParen) {
            // Left parenthesis goes to operator stack
            opStack.push(tok);
        }
        else if (tok.type == TokenType::RParen) {
            // Pop operators to output until we hit left parenthesis
            bool foundLparen = false;
            while (!opStack.empty()) {
                if (opStack.top().type == TokenType::LParen) {
                    foundLparen = true;
                    opStack.pop(); // Discard the '('
                    break;
                }
                output.push_back(opStack.top());
                opStack.pop();
            }
            if (!foundLparen) {
                throw runtime_error("Syntax error: Mismatched parentheses (missing '(')");
            }
        }
        else if (tok.type == TokenType::Operator || tok.type == TokenType::Keyword) {
            // While operator at stack top has greater or equal precedence, pop it to output
            while (!opStack.empty() && opStack.top().type != TokenType::LParen &&
                   getPrecedence(opStack.top().text) >= getPrecedence(tok.text)) {
                output.push_back(opStack.top());
                opStack.pop();
            }
            opStack.push(tok);
        }
    }

    // Pop any remaining operators on the stack to the output
    while (!opStack.empty()) {
        if (opStack.top().type == TokenType::LParen) {
            throw runtime_error("Syntax error: Mismatched parentheses (unclosed '(')");
        }
        output.push_back(opStack.top());
        opStack.pop();
    }

    return output;
}

// Helper to get column value from a student record
int getColumnValue(const string& columnName, const Student& student) {
    string col = toUpperCase(columnName);
    if (col == "ID")    return student.id;
    if (col == "AGE")   return student.age;
    if (col == "MARKS") return student.marks;
    throw runtime_error("Execution error: Unknown column name '" + columnName + "'");
}

// Evaluate comparison expression
bool evaluateComparison(int left, const string& op, int right) {
    if (op == ">")  return left > right;
    if (op == "<")  return left < right;
    if (op == ">=") return left >= right;
    if (op == "<=") return left <= right;
    if (op == "=")  return left == right;
    if (op == "!=") return left != right;
    throw runtime_error("Execution error: Unsupported comparison operator '" + op + "'");
}

// Evaluate a postfix token vector against a specific student record
bool evaluatePostfix(const vector<Token>& postfix, const Student& student) {
    stack<int> evalStack;

    for (const auto& tok : postfix) {
        if (tok.type == TokenType::Number) {
            evalStack.push(stoi(tok.text));
        }
        else if (tok.type == TokenType::Identifier) {
            evalStack.push(getColumnValue(tok.text, student));
        }
        else if (tok.type == TokenType::Operator) {
            if (evalStack.size() < 2) {
                throw runtime_error("Evaluation error: Malformed postfix expression");
            }
            int right = evalStack.top(); evalStack.pop();
            int left  = evalStack.top(); evalStack.pop();
            
            bool res = evaluateComparison(left, tok.text, right);
            evalStack.push(res ? 1 : 0);
        }
        else if (tok.type == TokenType::Keyword) {
            if (evalStack.size() < 2) {
                throw runtime_error("Evaluation error: Malformed logical expression");
            }
            int right = evalStack.top(); evalStack.pop();
            int left  = evalStack.top(); evalStack.pop();

            if (tok.text == "AND") {
                evalStack.push((left && right) ? 1 : 0);
            } else if (tok.text == "OR") {
                evalStack.push((left || right) ? 1 : 0);
            }
        }
    }

    if (evalStack.size() != 1) {
        throw runtime_error("Evaluation error: Expression did not reduce to a single value");
    }

    return evalStack.top() != 0;
}

int main() {
    // Hardcoded dataset of students
    vector<Student> db = {
        {1, "Aarav",   19, 78},
        {2, "Diya",    20, 91},
        {3, "Kabir",   18, 55},
        {4, "Meera",   22, 88},
        {5, "Rohan",   19, 42},
        {6, "Sneha",   21, 95},
        {7, "Vivaan",  20, 84}
    };

    cout << "=====================================================\n";
    cout << "  Database Query Parser (Dijkstra's Shunting-Yard)   \n";
    cout << "=====================================================\n\n";

    cout << "Available columns: id, age, marks\n";
    cout << "Available Student Records:\n";
    cout << "----------------------------------------------\n";
    cout << "ID\tName\tAge\tMarks\n";
    cout << "----------------------------------------------\n";
    for (const auto& s : db) {
        cout << s.id << "\t" << s.name << "\t" << s.age << "\t" << s.marks << "\n";
    }
    cout << "----------------------------------------------\n\n";

    // Standard test query
    string query = "id > 2 AND (marks >= 80 OR age < 20)";
    
    cout << "Enter a custom WHERE clause or press Enter to run the default:\n";
    cout << "Default: " << query << "\n";
    cout << "WHERE > ";
    
    string inputQuery;
    getline(cin, inputQuery);
    if (!inputQuery.empty()) {
        query = inputQuery;
    }

    try {
        // Step 1: Lexing
        vector<Token> tokens = tokenize(query);
        
        cout << "\n[Step 1: Lexical Analysis (Tokens)]\n";
        for (const auto& tok : tokens) {
            cout << "[" << tok.text << "] ";
        }
        cout << "\n";

        // Step 2: Shunting-Yard (Infix to Postfix RPN)
        vector<Token> postfix = shuntingYard(tokens);

        cout << "\n[Step 2: Shunting-Yard Conversion (Postfix / RPN)]\n";
        for (const auto& tok : postfix) {
            cout << tok.text << " ";
        }
        cout << "\n\n";

        // Step 3: Evaluation & Filtering
        cout << "[Step 3: Evaluating Records against WHERE clause]\n";
        cout << "Matching students:\n";
        cout << "----------------------------------------------\n";
        cout << "ID\tName\tAge\tMarks\n";
        cout << "----------------------------------------------\n";
        
        int matchCount = 0;
        for (const auto& s : db) {
            if (evaluatePostfix(postfix, s)) {
                cout << s.id << "\t" << s.name << "\t" << s.age << "\t" << s.marks << "\n";
                matchCount++;
            }
        }
        
        if (matchCount == 0) {
            cout << "No matching records found.\n";
        }
        cout << "----------------------------------------------\n";
        cout << "Total matched: " << matchCount << " records.\n";

    } catch (const exception& e) {
        cerr << "\nError: " << e.what() << "\n";
    }

    return 0;
}
