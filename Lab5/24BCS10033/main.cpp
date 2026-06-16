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

// =============================================================================
// Part 1: Dijkstra's Shunting Yard & AST Structures
// =============================================================================

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

enum class TokenType { INTEGER, STRING, IDENTIFIER, OPERATOR, LPAREN, RPAREN };

struct Token {
    TokenType type;
    string value;
};

enum class ASTNodeType { INTEGER_LITERAL, STRING_LITERAL, IDENTIFIER, BINARY_OP, UNARY_OP };

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
            if (operatorStack.empty()) throw runtime_error("Mismatched parentheses");
            operatorStack.pop();
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
                if (astStack.empty()) throw runtime_error("Malformed expression");
                ASTNode* operand = astStack.top();
                astStack.pop();
                astStack.push(new ASTNode(ASTNodeType::UNARY_OP, token.value, operand));
            } else {
                if (astStack.size() < 2) throw runtime_error("Malformed expression");
                ASTNode* right = astStack.top();
                astStack.pop();
                ASTNode* left = astStack.top();
                astStack.pop();
                astStack.push(new ASTNode(ASTNodeType::BINARY_OP, token.value, left, right));
            }
        }
    }
    if (astStack.size() != 1) {
        while (!astStack.empty()) { delete astStack.top(); astStack.pop(); }
        throw runtime_error("Malformed expression");
    }
    return astStack.top();
}

void printAST(ASTNode* node, int depth = 0) {
    if (!node) return;
    printAST(node->right, depth + 1);
    for (int i = 0; i < depth; ++i) cout << "    ";
    cout << node->value << "\n";
    printAST(node->left, depth + 1);
}

// =============================================================================
// Part 2: SQL Query Parser
// =============================================================================

enum class SQLTokenType { SELECT, FROM, WHERE, COMMA, IDENTIFIER, LITERAL_INT, LITERAL_STRING, OPERATOR, LPAREN, RPAREN };

struct SQLToken {
    SQLTokenType type;
    string value;
};

vector<SQLToken> tokenizeSQL(const string& sql) {
    vector<SQLToken> tokens;
    size_t i = 0;
    while (i < sql.length()) {
        char c = sql[i];
        if (isspace(c)) { i++; continue; }
        if (c == ',') { tokens.push_back({SQLTokenType::COMMA, ","}); i++; continue; }
        if (c == '(') { tokens.push_back({SQLTokenType::LPAREN, "("}); i++; continue; }
        if (c == ')') { tokens.push_back({SQLTokenType::RPAREN, ")"}); i++; continue; }
        if (c == '\'') {
            string strVal = "";
            i++;
            while (i < sql.length() && sql[i] != '\'') {
                strVal += sql[i];
                i++;
            }
            if (i >= sql.length()) throw runtime_error("Unterminated string literal");
            i++;
            tokens.push_back({SQLTokenType::LITERAL_STRING, strVal});
            continue;
        }
        if (isdigit(c)) {
            string numVal = "";
            while (i < sql.length() && isdigit(sql[i])) { numVal += sql[i]; i++; }
            tokens.push_back({SQLTokenType::LITERAL_INT, numVal});
            continue;
        }
        if (c == '=' || c == '!' || c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/') {
            string op = "";
            op += c;
            i++;
            if (i < sql.length()) {
                char nextC = sql[i];
                if ((c == '!' && nextC == '=') || (c == '<' && nextC == '=') || (c == '>' && nextC == '=') || (c == '<' && nextC == '>')) {
                    op += nextC;
                    i++;
                }
            }
            if (op == "<>") op = "!=";
            tokens.push_back({SQLTokenType::OPERATOR, op});
            continue;
        }
        if (isalpha(c) || c == '_') {
            string ident = "";
            while (i < sql.length() && (isalnum(sql[i]) || sql[i] == '_')) { ident += sql[i]; i++; }
            string upperIdent = ident;
            for (auto& ch : upperIdent) ch = toupper(ch);

            if (upperIdent == "SELECT") tokens.push_back({SQLTokenType::SELECT, upperIdent});
            else if (upperIdent == "FROM") tokens.push_back({SQLTokenType::FROM, upperIdent});
            else if (upperIdent == "WHERE") tokens.push_back({SQLTokenType::WHERE, upperIdent});
            else if (upperIdent == "AND" || upperIdent == "OR" || upperIdent == "NOT") tokens.push_back({SQLTokenType::OPERATOR, upperIdent});
            else tokens.push_back({SQLTokenType::IDENTIFIER, ident});
            continue;
        }
        throw runtime_error("Invalid character in SQL");
    }
    return tokens;
}

struct SQLQuery {
    string tableName;
    vector<string> columns;
    ASTNode* whereClause = nullptr;
    ~SQLQuery() { delete whereClause; }
};

SQLQuery parseSQL(const string& sqlQueryStr) {
    vector<SQLToken> tokens = tokenizeSQL(sqlQueryStr);
    size_t idx = 0;
    if (idx >= tokens.size() || tokens[idx].type != SQLTokenType::SELECT) throw runtime_error("Syntax error: query must start with SELECT");
    idx++;

    SQLQuery query;
    bool expectColumn = true;
    while (idx < tokens.size() && tokens[idx].type != SQLTokenType::FROM) {
        if (expectColumn) {
            if (tokens[idx].type == SQLTokenType::IDENTIFIER || (tokens[idx].type == SQLTokenType::OPERATOR && tokens[idx].value == "*")) {
                query.columns.push_back(tokens[idx].value);
                idx++;
                expectColumn = false;
            } else throw runtime_error("Expected column or *");
        } else {
            if (tokens[idx].type == SQLTokenType::COMMA) { idx++; expectColumn = true; }
            else throw runtime_error("Expected comma or FROM");
        }
    }
    if (expectColumn) throw runtime_error("Trailing comma in SELECT");
    if (idx >= tokens.size() || tokens[idx].type != SQLTokenType::FROM) throw runtime_error("Missing FROM");
    idx++;
    if (idx >= tokens.size() || tokens[idx].type != SQLTokenType::IDENTIFIER) throw runtime_error("Expected table name");
    query.tableName = tokens[idx].value;
    idx++;

    if (idx < tokens.size()) {
        if (tokens[idx].type != SQLTokenType::WHERE) throw runtime_error("Expected WHERE clause");
        idx++;
        vector<Token> whereExprTokens;
        while (idx < tokens.size()) {
            Token t;
            if (tokens[idx].type == SQLTokenType::IDENTIFIER) t.type = TokenType::IDENTIFIER;
            else if (tokens[idx].type == SQLTokenType::LITERAL_INT) t.type = TokenType::INTEGER;
            else if (tokens[idx].type == SQLTokenType::LITERAL_STRING) t.type = TokenType::STRING;
            else if (tokens[idx].type == SQLTokenType::OPERATOR) t.type = TokenType::OPERATOR;
            else if (tokens[idx].type == SQLTokenType::LPAREN) t.type = TokenType::LPAREN;
            else if (tokens[idx].type == SQLTokenType::RPAREN) t.type = TokenType::RPAREN;
            t.value = tokens[idx].value;
            whereExprTokens.push_back(t);
            idx++;
        }
        if (whereExprTokens.empty()) throw runtime_error("Empty WHERE clause");
        query.whereClause = buildAST(shuntingYard(whereExprTokens));
    }
    return query;
}

void printSQLQuery(const SQLQuery& query) {
    cout << "Table Name: " << query.tableName << "\n";
    cout << "Columns:    ";
    for (size_t i = 0; i < query.columns.size(); ++i) {
        cout << query.columns[i];
        if (i + 1 < query.columns.size()) cout << ", ";
    }
    cout << "\n";
    if (query.whereClause) {
        cout << "WHERE Clause AST (rotated 90 deg):\n";
        printAST(query.whereClause, 0);
    } else {
        cout << "WHERE Clause: [None]\n";
    }
}

void testQuery(const string& sqlStr) {
    cout << "========================================================\n";
    cout << "Parsing SQL Query: " << sqlStr << "\n";
    try {
        SQLQuery query = parseSQL(sqlStr);
        printSQLQuery(query);
    } catch (const exception& ex) {
        cout << "SQL Parsing Error: " << ex.what() << "\n";
    }
    cout << "========================================================\n\n";
}

int main() {
    cout << "========================================================\n";
    cout << "LAB 5: SQL QUERY PARSER WITH AST CONSTRUCTION\n";
    cout << "========================================================\n";
    
    // Test Case 1: Simple SELECT query with columns and table, no WHERE
    testQuery("SELECT id, name, salary FROM employees");

    // Test Case 2: SELECT * with WHERE clause
    testQuery("SELECT * FROM students WHERE grade = 'A'");

    // Test Case 3: SELECT with columns and compound WHERE clause
    testQuery("SELECT title, author FROM books WHERE year > 2000 AND price <= 50");

    // Test Case 4: Complex WHERE clause with parentheses and logical NOT
    testQuery("SELECT name, department FROM staff WHERE (age >= 18 AND status = 'active') OR NOT dept = 'HR'");

    return 0;
}
