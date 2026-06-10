#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cctype>
#include <unordered_map>
#include <stack>
#include <stdexcept>
#include <algorithm>

using namespace std;

// Value representation for variable context and expression evaluation
struct Value {
    enum class Type { INT, STRING, BOOL, NULL_VAL } type;
    int intVal = 0;
    string strVal = "";
    bool boolVal = false;

    Value() : type(Type::NULL_VAL) {}
    Value(int v) : type(Type::INT), intVal(v) {}
    Value(const string& v) : type(Type::STRING), strVal(v) {}
    Value(const char* v) : type(Type::STRING), strVal(v) {}
    Value(bool v) : type(Type::BOOL), boolVal(v) {}

    void print() const {
        if (type == Type::INT) cout << intVal;
        else if (type == Type::STRING) cout << "'" << strVal << "'";
        else if (type == Type::BOOL) cout << (boolVal ? "TRUE" : "FALSE");
        else cout << "NULL";
    }

    string toString() const {
        if (type == Type::INT) return to_string(intVal);
        if (type == Type::STRING) return strVal;
        if (type == Type::BOOL) return boolVal ? "true" : "false";
        return "null";
    }
};

// Token types for the parser
enum class TokenType {
    INTEGER,
    STRING,
    IDENTIFIER,
    OPERATOR,
    LPAREN,
    RPAREN
};

struct Token {
    TokenType type;
    string value;

    void print() const {
        switch (type) {
            case TokenType::INTEGER: cout << "[INT:" << value << "]"; break;
            case TokenType::STRING: cout << "[STR:\"" << value << "\"]"; break;
            case TokenType::IDENTIFIER: cout << "[ID:" << value << "]"; break;
            case TokenType::OPERATOR: cout << "[OP:" << value << "]"; break;
            case TokenType::LPAREN: cout << "[LPAREN]"; break;
            case TokenType::RPAREN: cout << "[RPAREN]"; break;
        }
    }
};

// AST Node representation
enum class ASTNodeType {
    INTEGER_LITERAL,
    STRING_LITERAL,
    IDENTIFIER,
    BINARY_OP,
    UNARY_OP
};

struct ASTNode {
    ASTNodeType type;
    string value;
    ASTNode* left = nullptr;
    ASTNode* right = nullptr;

    ASTNode(ASTNodeType t, string v, ASTNode* l = nullptr, ASTNode* r = nullptr)
        : type(t), value(v), left(l), right(r) {}

    ~ASTNode() {
        delete left;
        delete right;
    }
};

// Operator properties
// SQL standard precedence: Arithmetic > Comparison > NOT > AND > OR
int getPrecedence(const string& op) {
    string upperOp = op;
    for (auto& c : upperOp) c = toupper(c);

    if (upperOp == "*" || upperOp == "/") return 5;
    if (upperOp == "+" || upperOp == "-") return 4;
    if (upperOp == "=" || upperOp == "!=" || upperOp == "<" || upperOp == ">" || upperOp == "<=" || upperOp == ">=") return 3;
    if (upperOp == "NOT") return 2;
    if (upperOp == "AND") return 1;
    if (upperOp == "OR") return 0;
    return -1;
}

bool isRightAssociative(const string& op) {
    string upperOp = op;
    for (auto& c : upperOp) c = toupper(c);
    if (upperOp == "NOT") return true;
    return false;
}

bool isUnary(const string& op) {
    string upperOp = op;
    for (auto& c : upperOp) c = toupper(c);
    if (upperOp == "NOT") return true;
    return false;
}

// Tokenizer
vector<Token> tokenize(const string& expr) {
    vector<Token> tokens;
    size_t i = 0;
    while (i < expr.length()) {
        char c = expr[i];
        if (isspace(c)) {
            i++;
            continue;
        }

        // Parentheses
        if (c == '(') {
            tokens.push_back({TokenType::LPAREN, "("});
            i++;
            continue;
        }
        if (c == ')') {
            tokens.push_back({TokenType::RPAREN, ")"});
            i++;
            continue;
        }

        // String literals starting with single quote
        if (c == '\'') {
            string strVal = "";
            i++; // skip open quote
            while (i < expr.length() && expr[i] != '\'') {
                strVal += expr[i];
                i++;
            }
            if (i >= expr.length()) {
                throw runtime_error("Unterminated string literal starting at position " + to_string(i));
            }
            i++; // skip close quote
            tokens.push_back({TokenType::STRING, strVal});
            continue;
        }

        // Numeric literals
        if (isdigit(c)) {
            string numVal = "";
            while (i < expr.length() && isdigit(expr[i])) {
                numVal += expr[i];
                i++;
            }
            tokens.push_back({TokenType::INTEGER, numVal});
            continue;
        }

        // Multi-character and single-character operators
        if (c == '=' || c == '!' || c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/') {
            string op = "";
            op += c;
            i++;
            if (i < expr.length()) {
                char nextC = expr[i];
                if ((c == '!' && nextC == '=') ||
                    (c == '<' && nextC == '=') ||
                    (c == '>' && nextC == '=') ||
                    (c == '<' && nextC == '>')) {
                    op += nextC;
                    i++;
                }
            }
            // Normalize <> to != for consistency
            if (op == "<>") op = "!=";
            tokens.push_back({TokenType::OPERATOR, op});
            continue;
        }

        // Identifiers and Keywords
        if (isalpha(c) || c == '_') {
            string ident = "";
            while (i < expr.length() && (isalnum(expr[i]) || expr[i] == '_')) {
                ident += expr[i];
                i++;
            }
            string upperIdent = ident;
            for (auto& ch : upperIdent) ch = toupper(ch);

            if (upperIdent == "AND" || upperIdent == "OR" || upperIdent == "NOT") {
                tokens.push_back({TokenType::OPERATOR, upperIdent});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, ident});
            }
            continue;
        }

        throw runtime_error("Invalid character in expression: " + string(1, c));
    }
    return tokens;
}

// Shunting Yard algorithm (Infix to Postfix)
vector<Token> shuntingYard(const vector<Token>& tokens) {
    vector<Token> outputQueue;
    stack<Token> operatorStack;

    for (const auto& token : tokens) {
        if (token.type == TokenType::INTEGER || token.type == TokenType::STRING || token.type == TokenType::IDENTIFIER) {
            outputQueue.push_back(token);
        } else if (token.type == TokenType::LPAREN) {
            operatorStack.push(token);
        } else if (token.type == TokenType::RPAREN) {
            while (!operatorStack.empty() && operatorStack.top().type != TokenType::LPAREN) {
                outputQueue.push_back(operatorStack.top());
                operatorStack.pop();
            }
            if (operatorStack.empty()) {
                throw runtime_error("Mismatched parentheses (missing '(')");
            }
            operatorStack.pop(); // Pop the '('
        } else if (token.type == TokenType::OPERATOR) {
            string op1 = token.value;
            int prec1 = getPrecedence(op1);
            bool rightAssoc1 = isRightAssociative(op1);

            while (!operatorStack.empty() && operatorStack.top().type == TokenType::OPERATOR) {
                string op2 = operatorStack.top().value;
                int prec2 = getPrecedence(op2);

                if ((!rightAssoc1 && prec1 <= prec2) || (rightAssoc1 && prec1 < prec2)) {
                    outputQueue.push_back(operatorStack.top());
                    operatorStack.pop();
                } else {
                    break;
                }
            }
            operatorStack.push(token);
        }
    }

    while (!operatorStack.empty()) {
        if (operatorStack.top().type == TokenType::LPAREN || operatorStack.top().type == TokenType::RPAREN) {
            throw runtime_error("Mismatched parentheses");
        }
        outputQueue.push_back(operatorStack.top());
        operatorStack.pop();
    }

    return outputQueue;
}

// Build AST from Postfix
ASTNode* buildAST(const vector<Token>& postfix) {
    stack<ASTNode*> astStack;

    for (const auto& token : postfix) {
        if (token.type == TokenType::INTEGER) {
            astStack.push(new ASTNode(ASTNodeType::INTEGER_LITERAL, token.value));
        } else if (token.type == TokenType::STRING) {
            astStack.push(new ASTNode(ASTNodeType::STRING_LITERAL, token.value));
        } else if (token.type == TokenType::IDENTIFIER) {
            astStack.push(new ASTNode(ASTNodeType::IDENTIFIER, token.value));
        } else if (token.type == TokenType::OPERATOR) {
            if (isUnary(token.value)) {
                if (astStack.empty()) {
                    throw runtime_error("Malformed postfix expression: not enough operands for unary operator " + token.value);
                }
                ASTNode* operand = astStack.top();
                astStack.pop();
                astStack.push(new ASTNode(ASTNodeType::UNARY_OP, token.value, operand));
            } else {
                if (astStack.size() < 2) {
                    throw runtime_error("Malformed postfix expression: not enough operands for binary operator " + token.value);
                }
                ASTNode* right = astStack.top();
                astStack.pop();
                ASTNode* left = astStack.top();
                astStack.pop();
                astStack.push(new ASTNode(ASTNodeType::BINARY_OP, token.value, left, right));
            }
        }
    }

    if (astStack.size() != 1) {
        while (!astStack.empty()) {
            delete astStack.top();
            astStack.pop();
        }
        throw runtime_error("Malformed expression: stack final size is " + to_string(astStack.size()));
    }

    return astStack.top();
}

// Evaluate AST against variable context
Value evaluateAST(ASTNode* node, const unordered_map<string, Value>& row) {
    if (!node) return Value();

    if (node->type == ASTNodeType::INTEGER_LITERAL) {
        return Value(stoi(node->value));
    }
    if (node->type == ASTNodeType::STRING_LITERAL) {
        return Value(node->value);
    }
    if (node->type == ASTNodeType::IDENTIFIER) {
        if (row.find(node->value) != row.end()) {
            return row.at(node->value);
        }
        throw runtime_error("Variable/Column not found in context: " + node->value);
    }

    if (node->type == ASTNodeType::UNARY_OP) {
        Value val = evaluateAST(node->left, row);
        string op = node->value;
        for (auto& c : op) c = toupper(c);

        if (op == "NOT") {
            if (val.type != Value::Type::BOOL) {
                throw runtime_error("NOT operator requires boolean operand");
            }
            return Value(!val.boolVal);
        }
        throw runtime_error("Unknown unary operator: " + node->value);
    }

    if (node->type == ASTNodeType::BINARY_OP) {
        Value leftVal = evaluateAST(node->left, row);
        Value rightVal = evaluateAST(node->right, row);

        string op = node->value;
        for (auto& c : op) c = toupper(c);

        if (op == "+") {
            return Value(leftVal.intVal + rightVal.intVal);
        }
        if (op == "-") {
            return Value(leftVal.intVal - rightVal.intVal);
        }
        if (op == "*") {
            return Value(leftVal.intVal * rightVal.intVal);
        }
        if (op == "/") {
            if (rightVal.intVal == 0) throw runtime_error("Division by zero");
            return Value(leftVal.intVal / rightVal.intVal);
        }
        if (op == "=") {
            if (leftVal.type == Value::Type::INT && rightVal.type == Value::Type::INT) {
                return Value(leftVal.intVal == rightVal.intVal);
            }
            return Value(leftVal.toString() == rightVal.toString());
        }
        if (op == "!=") {
            if (leftVal.type == Value::Type::INT && rightVal.type == Value::Type::INT) {
                return Value(leftVal.intVal != rightVal.intVal);
            }
            return Value(leftVal.toString() != rightVal.toString());
        }
        if (op == "<") {
            if (leftVal.type == Value::Type::INT && rightVal.type == Value::Type::INT) {
                return Value(leftVal.intVal < rightVal.intVal);
            }
            return Value(leftVal.toString() < rightVal.toString());
        }
        if (op == ">") {
            if (leftVal.type == Value::Type::INT && rightVal.type == Value::Type::INT) {
                return Value(leftVal.intVal > rightVal.intVal);
            }
            return Value(leftVal.toString() > rightVal.toString());
        }
        if (op == "<=") {
            if (leftVal.type == Value::Type::INT && rightVal.type == Value::Type::INT) {
                return Value(leftVal.intVal <= rightVal.intVal);
            }
            return Value(leftVal.toString() <= rightVal.toString());
        }
        if (op == ">=") {
            if (leftVal.type == Value::Type::INT && rightVal.type == Value::Type::INT) {
                return Value(leftVal.intVal >= rightVal.intVal);
            }
            return Value(leftVal.toString() >= rightVal.toString());
        }
        if (op == "AND") {
            if (leftVal.type != Value::Type::BOOL || rightVal.type != Value::Type::BOOL) {
                throw runtime_error("AND operator requires boolean operands");
            }
            return Value(leftVal.boolVal && rightVal.boolVal);
        }
        if (op == "OR") {
            if (leftVal.type != Value::Type::BOOL || rightVal.type != Value::Type::BOOL) {
                throw runtime_error("OR operator requires boolean operands");
            }
            return Value(leftVal.boolVal || rightVal.boolVal);
        }

        throw runtime_error("Unknown binary operator: " + node->value);
    }

    return Value();
}

// Print AST tree structure sideways
void printAST(ASTNode* node, int depth = 0) {
    if (!node) return;
    printAST(node->right, depth + 1);
    for (int i = 0; i < depth; ++i) cout << "    ";
    cout << node->value << "\n";
    printAST(node->left, depth + 1);
}

// Test runner helper
void runTest(const string& expr, const unordered_map<string, Value>& row) {
    cout << "=============================================\n";
    cout << "Input Infix: " << expr << "\n";
    try {
        // Tokenize
        vector<Token> tokens = tokenize(expr);
        cout << "Tokens:      ";
        for (const auto& t : tokens) {
            t.print();
            cout << " ";
        }
        cout << "\n";

        // Shunting Yard
        vector<Token> postfix = shuntingYard(tokens);
        cout << "Postfix RPN: ";
        for (const auto& t : postfix) {
            cout << t.value << " ";
        }
        cout << "\n\n";

        // Build AST
        ASTNode* root = buildAST(postfix);
        cout << "AST Tree Representation (rotated 90 deg):\n";
        printAST(root, 0);
        cout << "\n";

        // Evaluate
        Value result = evaluateAST(root, row);
        cout << "Evaluation Result with Context:\n";
        for (const auto& pair : row) {
            cout << "  " << pair.first << " = ";
            pair.second.print();
            cout << "\n";
        }
        cout << "  --> ";
        result.print();
        cout << "\n";

        delete root;
    } catch (const exception& ex) {
        cout << "Error: " << ex.what() << "\n";
    }
    cout << "=============================================\n\n";
}

int main() {
    cout << "--- Lab 6: Shunting Yard Algorithm & Expression Parsing ---\n\n";

    // Context for variables
    unordered_map<string, Value> context1 = {
        {"age", Value(25)},
        {"salary", Value(55000)},
        {"dept", Value("Engineering")},
        {"status", Value("active")}
    };

    // Test cases
    // 1. Simple math expression
    runTest("(10 + 5) * 2 - 8 / 4", {});

    // 2. Relational comparison
    runTest("age >= 21", context1);

    // 3. Logical AND and OR
    runTest("age > 20 AND status = 'active'", context1);

    // 4. Operator precedence (AND vs OR vs arithmetic)
    runTest("age > 30 OR status = 'active' AND salary >= 50000", context1);

    // 5. Complex precedence with nested parentheses
    runTest("(age - 5 >= 20) AND (dept = 'Engineering' OR status = 'terminated')", context1);

    // 6. Logical NOT
    runTest("NOT status = 'inactive'", context1);

    return 0;
}
