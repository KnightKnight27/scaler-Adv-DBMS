#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <stack>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <iomanip>

// ---------------------------------------------------------------------
// SQL Lexical Parser & Postfix (RPN) Query Evaluator
// Student: Nandani Kumari (24bcs10317)
// Course: Advanced Database Management Systems (ADBMS)
// ---------------------------------------------------------------------

using DataTuple = std::unordered_map<std::string, std::string>;

enum class QueryLexSymbol {
    SYM_IDENTIFIER,
    SYM_NUMBER,
    SYM_STRING,
    SYM_OPERATOR,
    SYM_LPAREN,
    SYM_RPAREN,
    SYM_KEYWORD
};

struct ParsedLexeme {
    QueryLexSymbol type;
    std::string text;
};

// SQL Statement Scanner
std::vector<ParsedLexeme> scanQuery(const std::string& queryText) {
    std::vector<ParsedLexeme> lexemes;
    size_t offset = 0;
    const size_t size = queryText.size();

    while (offset < size) {
        if (std::isspace(queryText[offset])) {
            ++offset;
            continue;
        }

        if (queryText[offset] == '(') {
            lexemes.push_back({QueryLexSymbol::SYM_LPAREN, "("});
            ++offset;
            continue;
        }
        if (queryText[offset] == ')') {
            lexemes.push_back({QueryLexSymbol::SYM_RPAREN, ")"});
            ++offset;
            continue;
        }

        // Logical and comparison operators
        if (queryText[offset] == '<' || queryText[offset] == '>' || queryText[offset] == '!' || queryText[offset] == '=') {
            std::string op(1, queryText[offset]);
            if (offset + 1 < size && queryText[offset + 1] == '=') {
                op += '=';
                ++offset;
            }
            lexemes.push_back({QueryLexSymbol::SYM_OPERATOR, op});
            ++offset;
            continue;
        }

        // Single quoted text strings
        if (queryText[offset] == '\'') {
            std::string strVal;
            ++offset;
            while (offset < size && queryText[offset] != '\'') {
                strVal += queryText[offset++];
            }
            ++offset; // Skip quote
            lexemes.push_back({QueryLexSymbol::SYM_STRING, strVal});
            continue;
        }

        // Numerical variables
        if (std::isdigit(queryText[offset])) {
            std::string numericVal;
            while (offset < size && (std::isdigit(queryText[offset]) || queryText[offset] == '.')) {
                numericVal += queryText[offset++];
            }
            lexemes.push_back({QueryLexSymbol::SYM_NUMBER, numericVal});
            continue;
        }

        // Alphabetic keywords & tags
        if (std::isalpha(queryText[offset]) || queryText[offset] == '_' || queryText[offset] == '*') {
            if (queryText[offset] == '*') {
                lexemes.push_back({QueryLexSymbol::SYM_IDENTIFIER, "*"});
                ++offset;
                continue;
            }
            std::string name;
            while (offset < size && (std::isalnum(queryText[offset]) || queryText[offset] == '_')) {
                name += queryText[offset++];
            }
            std::string formattedName = name;
            std::transform(formattedName.begin(), formattedName.end(), formattedName.begin(), ::toupper);
            
            if (formattedName == "SELECT" || formattedName == "FROM" || formattedName == "WHERE" ||
                formattedName == "AND" || formattedName == "OR" || formattedName == "NOT") {
                lexemes.push_back({QueryLexSymbol::SYM_KEYWORD, formattedName});
            } else {
                lexemes.push_back({QueryLexSymbol::SYM_IDENTIFIER, name});
            }
            continue;
        }

        ++offset;
    }
    return lexemes;
}

// Logic priority weight
int evaluatePrecedence(const std::string& logicOp) {
    if (logicOp == "OR")  return 1;
    if (logicOp == "AND") return 2;
    if (logicOp == "NOT") return 3;
    return 4;
}

// Shunting-Yard implementation to convert infix to postfix notation
std::vector<ParsedLexeme> parseToPostfix(const std::vector<ParsedLexeme>& infixList) {
    std::vector<ParsedLexeme> postfixList;
    std::stack<ParsedLexeme> operatorStack;

    for (const auto& lexeme : infixList) {
        switch (lexeme.type) {
        case QueryLexSymbol::SYM_IDENTIFIER:
        case QueryLexSymbol::SYM_NUMBER:
        case QueryLexSymbol::SYM_STRING:
            postfixList.push_back(lexeme);
            break;

        case QueryLexSymbol::SYM_OPERATOR:
        case QueryLexSymbol::SYM_KEYWORD: {
            while (!operatorStack.empty() &&
                   operatorStack.top().type != QueryLexSymbol::SYM_LPAREN &&
                   evaluatePrecedence(operatorStack.top().text) >= evaluatePrecedence(lexeme.text)) {
                postfixList.push_back(operatorStack.top());
                operatorStack.pop();
            }
            operatorStack.push(lexeme);
            break;
        }

        case QueryLexSymbol::SYM_LPAREN:
            operatorStack.push(lexeme);
            break;

        case QueryLexSymbol::SYM_RPAREN:
            while (!operatorStack.empty() && operatorStack.top().type != QueryLexSymbol::SYM_LPAREN) {
                postfixList.push_back(operatorStack.top());
                operatorStack.pop();
            }
            if (!operatorStack.empty()) {
                operatorStack.pop();
            }
            break;
        }
    }

    while (!operatorStack.empty()) {
        postfixList.push_back(operatorStack.top());
        operatorStack.pop();
    }
    return postfixList;
}

// Stack evaluation of RPN expression on row data
bool runPostfixFilter(const std::vector<ParsedLexeme>& rpnList, const DataTuple& row) {
    if (rpnList.empty()) return true;

    std::stack<std::string> stack;

    for (const auto& lexeme : rpnList) {
        if (lexeme.type == QueryLexSymbol::SYM_NUMBER || lexeme.type == QueryLexSymbol::SYM_STRING) {
            stack.push(lexeme.text);
        } else if (lexeme.type == QueryLexSymbol::SYM_IDENTIFIER) {
            auto it = row.find(lexeme.text);
            if (it == row.end()) {
                throw std::runtime_error("Column resolution failed: " + lexeme.text);
            }
            stack.push(it->second);
        } else {
            if (stack.size() < 2) continue;
            std::string rhs = stack.top(); stack.pop();
            std::string lhs = stack.top(); stack.pop();

            auto computeComparison = [&](auto opFunc) -> std::string {
                return opFunc(std::stod(lhs), std::stod(rhs)) ? "1" : "0";
            };

            if      (lexeme.text == "=")  stack.push(lhs == rhs ? "1" : "0");
            else if (lexeme.text == "!=") stack.push(lhs != rhs ? "1" : "0");
            else if (lexeme.text == ">")  stack.push(computeComparison(std::greater<double>{}));
            else if (lexeme.text == "<")  stack.push(computeComparison(std::less<double>{}));
            else if (lexeme.text == ">=") stack.push(computeComparison(std::greater_equal<double>{}));
            else if (lexeme.text == "<=") stack.push(computeComparison(std::less_equal<double>{}));
            else if (lexeme.text == "AND") stack.push((lhs == "1" && rhs == "1") ? "1" : "0");
            else if (lexeme.text == "OR")  stack.push((lhs == "1" || rhs == "1") ? "1" : "0");
        }
    }

    return !stack.empty() && stack.top() == "1";
}

// Parse selection and display records
void evaluateSelect(const std::string& sqlQuery, const std::vector<DataTuple>& relations) {
    auto parsedTokens = scanQuery(sqlQuery);

    std::vector<std::string> viewColumns;
    std::vector<ParsedLexeme> filterChain;

    size_t cursor = 0;
    if (cursor < parsedTokens.size() && parsedTokens[cursor].text == "SELECT") {
        ++cursor;
    }

    while (cursor < parsedTokens.size() && parsedTokens[cursor].text != "FROM") {
        if (parsedTokens[cursor].type == QueryLexSymbol::SYM_IDENTIFIER) {
            viewColumns.push_back(parsedTokens[cursor].text);
        }
        ++cursor;
    }

    if (cursor < parsedTokens.size()) ++cursor; // Skip FROM
    if (cursor < parsedTokens.size()) ++cursor; // Skip TableName

    if (cursor < parsedTokens.size() && parsedTokens[cursor].text == "WHERE") {
        ++cursor;
        while (cursor < parsedTokens.size()) {
            filterChain.push_back(parsedTokens[cursor++]);
        }
    }

    auto postfixFilter = parseToPostfix(filterChain);

    std::vector<std::string> displayHeader;
    if (!viewColumns.empty() && viewColumns[0] == "*") {
        if (!relations.empty()) {
            for (const auto& cell : relations[0]) {
                displayHeader.push_back(cell.first);
            }
        }
    } else {
        displayHeader = viewColumns;
    }

    // Output table headers
    for (const auto& headerName : displayHeader) {
        std::cout << std::left << std::setw(24) << headerName;
    }
    std::cout << "\n" << std::string(24 * displayHeader.size(), '=') << "\n";

    // Output table rows matching criteria
    for (const auto& row : relations) {
        if (runPostfixFilter(postfixFilter, row)) {
            for (const auto& headerName : displayHeader) {
                auto match = row.find(headerName);
                std::cout << std::left << std::setw(24)
                          << (match != row.end() ? match->second : "NULL");
            }
            std::cout << "\n";
        }
    }
    std::cout << "\n";
}

int main() {
    // Unique Dataset: Books library catalog
    std::vector<DataTuple> bookCatalog = {
        {{"id","1"},{"title","The Great Gatsby"},      {"genre","Fiction"},   {"price","15.99"},{"stock","120"}},
        {{"id","2"},{"title","A Brief History of Time"},{"genre","Science"},   {"price","24.50"},{"stock","45"}},
        {{"id","3"},{"title","To Kill a Mockingbird"},  {"genre","Fiction"},   {"price","12.99"},{"stock","150"}},
        {{"id","4"},{"title","The Selfish Gene"},       {"genre","Science"},   {"price","18.95"},{"stock","30"}},
        {{"id","5"},{"title","1984"},                   {"genre","Dystopian"}, {"price","14.99"},{"stock","200"}},
        {{"id","6"},{"title","Brave New World"},        {"genre","Dystopian"}, {"price","13.50"},{"stock","80"}},
        {{"id","7"},{"title","The Origin of Species"},  {"genre","Science"},   {"price","29.99"},{"stock","25"}}
    };

    struct SqlTestCase {
        std::string label;
        std::string stmt;
    };

    std::vector<SqlTestCase> testSuite = {
        {"Science books priced above 20.00",
         "SELECT title, price FROM books WHERE genre = 'Science' AND price > 20.00"},

        {"Books that are not in the Fiction genre",
         "SELECT title, genre FROM books WHERE genre != 'Fiction'"},

        {"Books with stock units between 40 and 150 (inclusive)",
         "SELECT title, stock, genre FROM books WHERE stock >= 40 AND stock <= 150"},

        {"Fiction or Dystopian publications with stock levels >= 100",
         "SELECT title, genre, stock FROM books WHERE (genre = 'Fiction' OR genre = 'Dystopian') AND stock >= 100"},

        {"Select all records (* wildcard)",
         "SELECT * FROM books"}
    };

    for (const auto& test : testSuite) {
        std::cout << ">>> Test: " << test.label << "\n";
        std::cout << "Query: " << test.stmt << "\n\n";
        evaluateSelect(test.stmt, bookCatalog);
    }

    return 0;
}
