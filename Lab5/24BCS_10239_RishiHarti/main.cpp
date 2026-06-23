#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <stack>
#include <queue>
#include <cctype>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <iomanip>
#include <cassert>

// ============================================================================
// PART 1: GENERIC ROW & VALUE REPRESENTATION
// ============================================================================

using Value = std::variant<int, std::string>;
using Row = std::unordered_map<std::string, Value>;

std::string valueToString(const Value& val) {
    if (std::holds_alternative<int>(val)) {
        return std::to_string(std::get<int>(val));
    }
    return std::get<std::string>(val);
}

bool compareValues(const Value& left, const Value& right, const std::string& op, bool& success) {
    success = true;
    if (left.index() != right.index()) {
        // Type mismatch. We can try to cast or fail.
        // For databases, let's treat mismatch as incompatible except if we can convert.
        success = false;
        return false;
    }

    if (std::holds_alternative<int>(left)) {
        int l = std::get<int>(left);
        int r = std::get<int>(right);
        if (op == "=") return l == r;
        if (op == "!=") return l != r;
        if (op == ">") return l > r;
        if (op == "<") return l < r;
        if (op == ">=") return l >= r;
        if (op == "<=") return l <= r;
    } else {
        const std::string& l = std::get<std::string>(left);
        const std::string& r = std::get<std::string>(right);
        if (op == "=") return l == r;
        if (op == "!=") return l != r;
        if (op == ">") return l > r;
        if (op == "<") return l < r;
        if (op == ">=") return l >= r;
        if (op == "<=") return l <= r;
    }
    success = false;
    return false;
}

// ============================================================================
// PART 2: LEXER & TOKENS
// ============================================================================

enum class TokenType {
    SELECT, FROM, WHERE, AND, OR, IDENTIFIER, NUMBER, STRING,
    GT, LT, GTE, LTE, EQ, NEQ, COMMA, LPAREN, RPAREN, END
};

struct Token {
    TokenType type;
    std::string text;
};

std::string tokenTypeToString(TokenType t) {
    switch (t) {
        case TokenType::SELECT: return "SELECT";
        case TokenType::FROM: return "FROM";
        case TokenType::WHERE: return "WHERE";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::STRING: return "STRING";
        case TokenType::GT: return ">";
        case TokenType::LT: return "<";
        case TokenType::GTE: return ">=";
        case TokenType::LTE: return "<=";
        case TokenType::EQ: return "=";
        case TokenType::NEQ: return "!=";
        case TokenType::COMMA: return ",";
        case TokenType::LPAREN: return "(";
        case TokenType::RPAREN: return ")";
        case TokenType::END: return "END";
    }
    return "UNKNOWN";
}

class Lexer {
private:
    std::string input;
    size_t pos;

    char peek() {
        if (pos >= input.size()) return '\0';
        return input[pos];
    }

    char advance() {
        if (pos >= input.size()) return '\0';
        return input[pos++];
    }

public:
    explicit Lexer(std::string sql) : input(std::move(sql)), pos(0) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (pos < input.size()) {
            char curr = peek();
            if (std::isspace(curr)) {
                advance();
                continue;
            }

            if (curr == '(') {
                tokens.push_back({TokenType::LPAREN, "("});
                advance();
                continue;
            }
            if (curr == ')') {
                tokens.push_back({TokenType::RPAREN, ")"});
                advance();
                continue;
            }
            if (curr == ',') {
                tokens.push_back({TokenType::COMMA, ","});
                advance();
                continue;
            }

            // Operators GTE, LTE, GT, LT, EQ, NEQ
            if (curr == '=') {
                tokens.push_back({TokenType::EQ, "="});
                advance();
                continue;
            }
            if (curr == '!') {
                advance();
                if (peek() == '=') {
                    tokens.push_back({TokenType::NEQ, "!="});
                    advance();
                } else {
                    throw std::runtime_error("Lexer Error: Unexpected character '!'");
                }
                continue;
            }
            if (curr == '>') {
                advance();
                if (peek() == '=') {
                    tokens.push_back({TokenType::GTE, ">="});
                    advance();
                } else {
                    tokens.push_back({TokenType::GT, ">"});
                }
                continue;
            }
            if (curr == '<') {
                advance();
                if (peek() == '=') {
                    tokens.push_back({TokenType::LTE, "<="});
                    advance();
                } else if (peek() == '>') {
                    tokens.push_back({TokenType::NEQ, "<>"}); // alternate NEQ
                    advance();
                } else {
                    tokens.push_back({TokenType::LT, "<"});
                }
                continue;
            }

            // String Literals
            if (curr == '\'') {
                advance(); // Consume opening quote
                std::string strVal;
                while (peek() != '\'' && peek() != '\0') {
                    strVal += advance();
                }
                if (peek() == '\0') {
                    throw std::runtime_error("Lexer Error: Unterminated string literal");
                }
                advance(); // Consume closing quote
                tokens.push_back({TokenType::STRING, strVal});
                continue;
            }

            // Identifiers / Keywords
            if (std::isalpha(curr) || curr == '_') {
                std::string word;
                while (std::isalnum(peek()) || peek() == '_') {
                    word += advance();
                }
                // Case insensitivity for keywords
                std::string upperWord = word;
                std::transform(upperWord.begin(), upperWord.end(), upperWord.begin(), ::toupper);

                if (upperWord == "SELECT") tokens.push_back({TokenType::SELECT, word});
                else if (upperWord == "FROM") tokens.push_back({TokenType::FROM, word});
                else if (upperWord == "WHERE") tokens.push_back({TokenType::WHERE, word});
                else if (upperWord == "AND") tokens.push_back({TokenType::AND, word});
                else if (upperWord == "OR") tokens.push_back({TokenType::OR, word});
                else tokens.push_back({TokenType::IDENTIFIER, word});
                continue;
            }

            // Number Literals
            if (std::isdigit(curr)) {
                std::string num;
                while (std::isdigit(peek())) {
                    num += advance();
                }
                tokens.push_back({TokenType::NUMBER, num});
                continue;
            }

            throw std::runtime_error(std::string("Lexer Error: Unrecognized character '") + curr + "'");
        }
        tokens.push_back({TokenType::END, ""});
        return tokens;
    }
};

// ============================================================================
// PART 3: MINIMAL SQL PARSER
// ============================================================================

struct SelectStatement {
    std::vector<std::string> columns; // Support multiple cols, e.g., ["name", "age"] or ["*"]
    std::string tableName;
    std::vector<Token> whereClauseInfix;
};

class SQLParser {
private:
    std::vector<Token> tokens;
    size_t pos;

    Token peek() {
        if (pos >= tokens.size()) return {TokenType::END, ""};
        return tokens[pos];
    }

    Token advance() {
        if (pos >= tokens.size()) return {TokenType::END, ""};
        return tokens[pos++];
    }

    void consume(TokenType type, const std::string& errMsg) {
        if (peek().type != type) {
            throw std::runtime_error(errMsg + " (Got '" + peek().text + "')");
        }
        advance();
    }

public:
    explicit SQLParser(std::vector<Token> toks) : tokens(std::move(toks)), pos(0) {}

    SelectStatement parse() {
        SelectStatement stmt;

        // SELECT
        consume(TokenType::SELECT, "Expected SELECT keyword");

        // Columns
        if (peek().type == TokenType::IDENTIFIER && peek().text == "*") {
            stmt.columns.push_back("*");
            advance();
        } else {
            while (true) {
                if (peek().type != TokenType::IDENTIFIER) {
                    throw std::runtime_error("Expected column name identifier in SELECT list");
                }
                stmt.columns.push_back(advance().text);
                if (peek().type == TokenType::COMMA) {
                    advance(); // Consume comma
                } else {
                    break;
                }
            }
        }

        // FROM
        consume(TokenType::FROM, "Expected FROM keyword");

        // Table Name
        if (peek().type != TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected table name identifier after FROM");
        }
        stmt.tableName = advance().text;

        // Optional WHERE clause
        if (peek().type == TokenType::WHERE) {
            advance(); // Consume WHERE
            while (peek().type != TokenType::END) {
                stmt.whereClauseInfix.push_back(advance());
            }
        }

        return stmt;
    }
};

// ============================================================================
// PART 4: DIJKSTRA'S SHUNTING-YARD EXPRESSION PARSER
// ============================================================================

class ShuntingYard {
private:
    static int getOperatorPrecedence(TokenType type) {
        switch (type) {
            case TokenType::OR:
                return 1;
            case TokenType::AND:
                return 2;
            case TokenType::EQ:
            case TokenType::NEQ:
            case TokenType::GT:
            case TokenType::LT:
            case TokenType::GTE:
            case TokenType::LTE:
                return 3;
            default:
                return 0;
        }
    }

    static bool isOperator(TokenType type) {
        return getOperatorPrecedence(type) > 0;
    }

public:
    static std::vector<Token> infixToPostfix(const std::vector<Token>& infix) {
        std::vector<Token> postfix;
        std::stack<Token> opStack;

        for (const auto& token : infix) {
            if (token.type == TokenType::IDENTIFIER || 
                token.type == TokenType::NUMBER || 
                token.type == TokenType::STRING) {
                // Operands push directly to output queue
                postfix.push_back(token);
            } 
            else if (token.type == TokenType::LPAREN) {
                opStack.push(token);
            } 
            else if (token.type == TokenType::RPAREN) {
                while (!opStack.empty() && opStack.top().type != TokenType::LPAREN) {
                    postfix.push_back(opStack.top());
                    opStack.pop();
                }
                if (opStack.empty()) {
                    throw std::runtime_error("Shunting Yard Error: Mismatched parentheses (missing '(' )");
                }
                opStack.pop(); // Pop '('
            } 
            else if (isOperator(token.type)) {
                while (!opStack.empty() && isOperator(opStack.top().type) &&
                       getOperatorPrecedence(opStack.top().type) >= getOperatorPrecedence(token.type)) {
                    postfix.push_back(opStack.top());
                    opStack.pop();
                }
                opStack.push(token);
            }
            else {
                throw std::runtime_error("Shunting Yard Error: Invalid token in WHERE clause expression: '" + token.text + "'");
            }
        }

        while (!opStack.empty()) {
            if (opStack.top().type == TokenType::LPAREN) {
                throw std::runtime_error("Shunting Yard Error: Mismatched parentheses (missing ')' )");
            }
            postfix.push_back(opStack.top());
            opStack.pop();
        }

        return postfix;
    }
};

// ============================================================================
// PART 5: POSTFIX EVALUATOR ENGINE
// ============================================================================

class Evaluator {
public:
    static bool evaluatePostfix(const std::vector<Token>& postfix, const Row& row) {
        if (postfix.empty()) return true; // No WHERE clause means match all

        std::stack<Value> valStack;

        for (const auto& token : postfix) {
            if (token.type == TokenType::NUMBER) {
                valStack.push(std::stoi(token.text));
            } 
            else if (token.type == TokenType::STRING) {
                valStack.push(token.text);
            } 
            else if (token.type == TokenType::IDENTIFIER) {
                // Resolve column reference
                auto it = row.find(token.text);
                if (it == row.end()) {
                    throw std::runtime_error("Evaluation Error: Column '" + token.text + "' not found in table schema.");
                }
                valStack.push(it->second);
            } 
            else {
                // Logical and Comparison Operators
                if (token.type == TokenType::AND || token.type == TokenType::OR) {
                    if (valStack.size() < 2) {
                        throw std::runtime_error("Evaluation Error: Insufficient operands for logical operator '" + token.text + "'");
                    }
                    Value rightVal = valStack.top(); valStack.pop();
                    Value leftVal = valStack.top(); valStack.pop();

                    if (!std::holds_alternative<int>(leftVal) || !std::holds_alternative<int>(rightVal)) {
                        throw std::runtime_error("Evaluation Error: Logical operators AND/OR require boolean/integer operands.");
                    }

                    int l = std::get<int>(leftVal);
                    int r = std::get<int>(rightVal);
                    int res = 0;
                    if (token.type == TokenType::AND) res = (l && r) ? 1 : 0;
                    else if (token.type == TokenType::OR) res = (l || r) ? 1 : 0;

                    valStack.push(res);
                } 
                else {
                    // Comparison Operator
                    if (valStack.size() < 2) {
                        throw std::runtime_error("Evaluation Error: Insufficient operands for comparison operator '" + token.text + "'");
                    }
                    Value rightVal = valStack.top(); valStack.pop();
                    Value leftVal = valStack.top(); valStack.pop();

                    bool compSuccess = false;
                    bool res = compareValues(leftVal, rightVal, token.text, compSuccess);
                    if (!compSuccess) {
                        throw std::runtime_error("Evaluation Error: Type mismatch or invalid comparison: '" +
                                                 valueToString(leftVal) + "' " + token.text + " '" + valueToString(rightVal) + "'");
                    }

                    valStack.push(res ? 1 : 0);
                }
            }
        }

        if (valStack.size() != 1) {
            throw std::runtime_error("Evaluation Error: Expression did not evaluate to a single result value.");
        }

        Value finalVal = valStack.top();
        if (!std::holds_alternative<int>(finalVal)) {
            throw std::runtime_error("Evaluation Error: Result value of WHERE filter must be a boolean/integer.");
        }

        return std::get<int>(finalVal) != 0;
    }
};

// ============================================================================
// PART 6: TEST SUITE & EXECUTOR
// ============================================================================

void printTable(const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& data) {
    if (headers.empty()) return;
    
    std::vector<size_t> colWidths(headers.size(), 0);
    for (size_t i = 0; i < headers.size(); i++) {
        colWidths[i] = headers[i].size();
    }
    for (const auto& row : data) {
        for (size_t i = 0; i < row.size(); i++) {
            if (i < colWidths.size()) {
                colWidths[i] = std::max(colWidths[i], row[i].size());
            }
        }
    }

    // Border line
    std::cout << "+";
    for (size_t w : colWidths) {
        std::cout << std::string(w + 2, '-') << "+";
    }
    std::cout << std::endl;

    // Headers
    std::cout << "|";
    for (size_t i = 0; i < headers.size(); i++) {
        std::cout << " \033[97m" << std::left << std::setw(colWidths[i]) << headers[i] << "\033[0m |";
    }
    std::cout << std::endl;

    // Border line
    std::cout << "+";
    for (size_t w : colWidths) {
        std::cout << std::string(w + 2, '-') << "+";
    }
    std::cout << std::endl;

    // Rows
    for (const auto& row : data) {
        std::cout << "|";
        for (size_t i = 0; i < row.size(); i++) {
            std::cout << " " << std::left << std::setw(colWidths[i]) << row[i] << " |";
        }
        std::cout << std::endl;
    }

    // Border line
    std::cout << "+";
    for (size_t w : colWidths) {
        std::cout << std::string(w + 2, '-') << "+";
    }
    std::cout << std::endl;
}

void executeAndRender(const std::string& sqlQuery, const std::vector<Row>& dataset) {
    std::cout << "\n\033[36mQuery:\033[0m \"" << sqlQuery << "\"" << std::endl;

    try {
        Lexer lexer(sqlQuery);
        auto tokens = lexer.tokenize();

        SQLParser parser(tokens);
        SelectStatement stmt = parser.parse();

        auto postfix = ShuntingYard::infixToPostfix(stmt.whereClauseInfix);

        // Determine columns to display
        std::vector<std::string> displayHeaders;
        if (stmt.columns.size() == 1 && stmt.columns[0] == "*") {
            // Get all columns from first row if dataset is not empty
            if (!dataset.empty()) {
                for (const auto& pair : dataset[0]) {
                    displayHeaders.push_back(pair.first);
                }
                std::sort(displayHeaders.begin(), displayHeaders.end());
            }
        } else {
            displayHeaders = stmt.columns;
        }

        std::vector<std::vector<std::string>> displayData;
        int rowCount = 0;
        for (const auto& row : dataset) {
            if (Evaluator::evaluatePostfix(postfix, row)) {
                std::vector<std::string> outputRow;
                for (const auto& col : displayHeaders) {
                    auto it = row.find(col);
                    if (it != row.end()) {
                        outputRow.push_back(valueToString(it->second));
                    } else {
                        outputRow.push_back("NULL");
                    }
                }
                displayData.push_back(outputRow);
                rowCount++;
            }
        }

        printTable(displayHeaders, displayData);
        std::cout << "\033[92m[" << rowCount << " row(s) returned]\033[0m" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\033[31mError Executing Query: " << e.what() << "\033[0m" << std::endl;
    }
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "    LAB 5: SHUNTING-YARD EXPRESSION ENGINE & SQL PARSER   " << std::endl;
    std::cout << "    Roll No: 24BCS10239 | Name: Rishi Harti" << std::endl;
    std::cout << "==========================================================" << std::endl;

    // Define mock employee database table rows
    std::vector<Row> employees = {
        {{"id", 1}, {"name", "Kartik"}, {"age", 20}, {"department", "Engineering"}},
        {{"id", 2}, {"name", "Krishank"}, {"age", 30}, {"department", "Engineering"}},
        {{"id", 3}, {"name", "Sandip"}, {"age", 15}, {"department", "Operations"}},
        {{"id", 4}, {"name", "Nitish"}, {"age", 17}, {"department", "HR"}},
        {{"id", 5}, {"name", "Kp"}, {"age", 20}, {"department", "Operations"}},
        {{"id", 6}, {"name", "Rishi"}, {"age", 22}, {"department", "Engineering"}}
    };

    // 1. Basic filter test
    executeAndRender("SELECT name, age FROM employees WHERE age > 18", employees);

    // 2. Logic operator test (AND)
    executeAndRender("SELECT name, age, department FROM employees WHERE department = 'Engineering' AND age >= 22", employees);

    // 3. Nested parentheses logic operator test (AND / OR)
    executeAndRender("SELECT id, name, age FROM employees WHERE (age < 18 OR id < 2) AND department != 'HR'", employees);

    // 4. Wildcard projection select test
    executeAndRender("SELECT * FROM employees WHERE department = 'Operations'", employees);

    // 5. Infix Shunting-yard trace test
    std::cout << "\n\033[33m[+] Tracing Infix-to-Postfix conversion for validation...\033[0m" << std::endl;
    std::string infixQuery = "(age < 18 OR id < 2)";
    Lexer lexer(infixQuery);
    auto tokens = lexer.tokenize();
    // Strip ending END token
    if (!tokens.empty() && tokens.back().type == TokenType::END) tokens.pop_back();

    std::cout << "    Infix String: " << infixQuery << std::endl;
    std::cout << "    Infix Tokens: ";
    for (const auto& t : tokens) std::cout << "[" << t.text << "] ";
    std::cout << std::endl;

    auto postfix = ShuntingYard::infixToPostfix(tokens);
    std::cout << "    Postfix Queue: ";
    for (const auto& t : postfix) std::cout << t.text << " ";
    std::cout << std::endl;

    // Assert correct RPN output
    assert(postfix.size() == 7);
    assert(postfix[0].text == "age");
    assert(postfix[1].text == "18");
    assert(postfix[2].text == "<");
    assert(postfix[3].text == "id");
    assert(postfix[4].text == "2");
    assert(postfix[5].text == "<");
    assert(postfix[6].text == "OR");

    std::cout << "\033[92m[+] Verification passed successfully! Dijkstra's Shunting-Yard engine is stable.\033[0m\n" << std::endl;

    return 0;
}
