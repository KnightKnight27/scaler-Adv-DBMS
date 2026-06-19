#include <algorithm>
#include <cctype>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct Book {
    int bookId;
    std::string title;
    std::string author;
    std::string genre;
    int year;
    int pages;
    int rating;
};

namespace {

enum class TokenType {
    Word,
    Number,
    StringLiteral,
    ComparisonOperator,
    Comma,
    Star,
    LeftParen,
    RightParen,
    End
};

struct Token {
    TokenType type;
    std::string text;
    std::string display;
    std::size_t position;
};

bool isIntegerField(const std::string& fieldName) {
    return fieldName == "bookId" || fieldName == "year" || fieldName == "pages" || fieldName == "rating";
}

std::string toUpperCopy(const std::string& input) {
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return result;
}

std::string toLowerCopy(const std::string& input) {
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

std::string canonicalFieldName(const std::string& tokenText) {
    static const std::unordered_map<std::string, std::string> fieldMap = {
        {"bookid", "bookId"},
        {"title", "title"},
        {"author", "author"},
        {"genre", "genre"},
        {"year", "year"},
        {"pages", "pages"},
        {"rating", "rating"},
    };

    const auto it = fieldMap.find(toLowerCopy(tokenText));
    return it == fieldMap.end() ? "" : it->second;
}

std::vector<Book> createBookTable() {
    return {
        {101, "The Lantern Code", "Nadia Brooks", "Fiction", 2018, 352, 5},
        {102, "Silent Map", "Owen Hart", "Mystery", 2016, 288, 4},
        {103, "Orbit of Dust", "Leena Park", "Science", 2021, 410, 5},
        {104, "Paper Boats", "Mira Sen", "Fiction", 2012, 224, 3},
        {105, "History of Tides", "Rafael Dean", "History", 2010, 498, 4},
        {106, "Winter Signal", "Anika Roy", "Mystery", 2019, 376, 5},
        {107, "Garden of Circuits", "Theo Martin", "Technology", 2023, 540, 4},
        {108, "Blue Hour Essays", "Sara Noel", "Nonfiction", 2017, 198, 4},
        {109, "Echoes in Copper", "Nadia Brooks", "Fiction", 2020, 318, 4},
    };
}

[[noreturn]] void throwTokenError(std::size_t position, const std::string& message) {
    std::ostringstream builder;
    builder << "Query token error near position " << position << ": " << message;
    throw std::runtime_error(builder.str());
}

std::vector<Token> tokenizeQuery(const std::string& query) {
    std::vector<Token> tokens;

    for (std::size_t index = 0; index < query.size();) {
        const unsigned char current = static_cast<unsigned char>(query[index]);

        if (std::isspace(current)) {
            ++index;
            continue;
        }

        if (query[index] == ',' ) {
            tokens.push_back({TokenType::Comma, ",", ",", index});
            ++index;
            continue;
        }

        if (query[index] == '*') {
            tokens.push_back({TokenType::Star, "*", "*", index});
            ++index;
            continue;
        }

        if (query[index] == '(') {
            tokens.push_back({TokenType::LeftParen, "(", "(", index});
            ++index;
            continue;
        }

        if (query[index] == ')') {
            tokens.push_back({TokenType::RightParen, ")", ")", index});
            ++index;
            continue;
        }

        if (query[index] == '"' || query[index] == '\'') {
            const char quote = query[index];
            const std::size_t start = index;
            ++index;
            std::string value;

            while (index < query.size() && query[index] != quote) {
                value.push_back(query[index]);
                ++index;
            }

            if (index == query.size()) {
                throwTokenError(start, "missing closing quote");
            }

            ++index;
            tokens.push_back({TokenType::StringLiteral, value, std::string(1, quote) + value + quote, start});
            continue;
        }

        if (query[index] == '<' || query[index] == '>' || query[index] == '!' || query[index] == '=') {
            const std::size_t start = index;
            std::string op(1, query[index]);
            ++index;

            if (index < query.size() && query[index] == '=') {
                op.push_back('=');
                ++index;
            }

            if (op == "!" || op == "<>" || op == "==") {
                throwTokenError(start, "unsupported comparison operator '" + op + "'");
            }

            tokens.push_back({TokenType::ComparisonOperator, op, op, start});
            continue;
        }

        if (std::isdigit(current) ||
            ((query[index] == '-' || query[index] == '+') && index + 1 < query.size() &&
             std::isdigit(static_cast<unsigned char>(query[index + 1])))) {
            const std::size_t start = index;
            std::size_t end = index + 1;

            while (end < query.size() && std::isdigit(static_cast<unsigned char>(query[end]))) {
                ++end;
            }

            const std::string value = query.substr(start, end - start);
            tokens.push_back({TokenType::Number, value, value, start});
            index = end;
            continue;
        }

        if (std::isalpha(current) || query[index] == '_') {
            const std::size_t start = index;
            std::size_t end = index + 1;

            while (end < query.size()) {
                const unsigned char next = static_cast<unsigned char>(query[end]);
                if (!std::isalnum(next) && query[end] != '_') {
                    break;
                }
                ++end;
            }

            const std::string word = query.substr(start, end - start);
            tokens.push_back({TokenType::Word, word, word, start});
            index = end;
            continue;
        }

        if (query[index] == ';' && index + 1 == query.size()) {
            ++index;
            continue;
        }

        throwTokenError(index, std::string("unexpected character '") + query[index] + "'");
    }

    tokens.push_back({TokenType::End, "", "<END>", query.size()});
    return tokens;
}

class ConditionNode {
public:
    virtual ~ConditionNode() = default;
    virtual bool evaluate(const Book& book) const = 0;
};

class ComparisonNode : public ConditionNode {
public:
    ComparisonNode(std::string fieldName, std::string compareOp, std::string literalValue)
        : field(std::move(fieldName)), op(std::move(compareOp)), value(std::move(literalValue)) {}

    bool evaluate(const Book& book) const override {
        if (isIntegerField(field)) {
            const int fieldValue = readNumericField(book, field);
            const int literal = std::stoi(value);

            if (op == "=") {
                return fieldValue == literal;
            }
            if (op == "!=") {
                return fieldValue != literal;
            }
            if (op == "<") {
                return fieldValue < literal;
            }
            if (op == "<=") {
                return fieldValue <= literal;
            }
            if (op == ">") {
                return fieldValue > literal;
            }
            return fieldValue >= literal;
        }

        const std::string fieldValue = readTextField(book, field);
        if (op == "=") {
            return fieldValue == value;
        }
        return fieldValue != value;
    }

private:
    static int readNumericField(const Book& book, const std::string& fieldName) {
        if (fieldName == "bookId") {
            return book.bookId;
        }
        if (fieldName == "year") {
            return book.year;
        }
        if (fieldName == "pages") {
            return book.pages;
        }
        return book.rating;
    }

    static std::string readTextField(const Book& book, const std::string& fieldName) {
        if (fieldName == "title") {
            return book.title;
        }
        if (fieldName == "author") {
            return book.author;
        }
        return book.genre;
    }

    std::string field;
    std::string op;
    std::string value;
};

class LogicalNode : public ConditionNode {
public:
    LogicalNode(std::string logicalOp, std::unique_ptr<ConditionNode> leftNode,
                std::unique_ptr<ConditionNode> rightNode)
        : op(std::move(logicalOp)), left(std::move(leftNode)), right(std::move(rightNode)) {}

    bool evaluate(const Book& book) const override {
        if (op == "AND") {
            return left->evaluate(book) && right->evaluate(book);
        }
        return left->evaluate(book) || right->evaluate(book);
    }

private:
    std::string op;
    std::unique_ptr<ConditionNode> left;
    std::unique_ptr<ConditionNode> right;
};

struct ParsedQuery {
    std::vector<std::string> selectedColumns;
    std::string tableName;
    bool hasWhereClause = false;
    std::unique_ptr<ConditionNode> whereClause;
};

class QueryParser {
public:
    explicit QueryParser(std::vector<Token> tokenStream) : tokens(std::move(tokenStream)) {}

    ParsedQuery parseQuery() {
        ParsedQuery query;

        expectKeyword("SELECT");
        query.selectedColumns = parseSelectedColumns();

        expectKeyword("FROM");
        query.tableName = parseTableName();

        if (matchKeyword("WHERE")) {
            query.hasWhereClause = true;
            query.whereClause = parseExpression();
        }

        if (!isAtEnd()) {
            const Token& token = peek();
            throw std::runtime_error("Unexpected token after the end of the query: '" + token.display + "'");
        }

        return query;
    }

private:
    std::unique_ptr<ConditionNode> parseExpression() {
        return parseOr();
    }

    std::unique_ptr<ConditionNode> parseOr() {
        std::unique_ptr<ConditionNode> node = parseAnd();

        while (matchKeyword("OR")) {
            std::unique_ptr<ConditionNode> right = parseAnd();
            node = std::make_unique<LogicalNode>("OR", std::move(node), std::move(right));
        }

        return node;
    }

    std::unique_ptr<ConditionNode> parseAnd() {
        std::unique_ptr<ConditionNode> node = parsePrimary();

        while (matchKeyword("AND")) {
            std::unique_ptr<ConditionNode> right = parsePrimary();
            node = std::make_unique<LogicalNode>("AND", std::move(node), std::move(right));
        }

        return node;
    }

    std::unique_ptr<ConditionNode> parsePrimary() {
        if (match(TokenType::LeftParen)) {
            std::unique_ptr<ConditionNode> node = parseExpression();
            expect(TokenType::RightParen, "Expected ')' to close the WHERE condition.");
            return node;
        }

        return parseComparison();
    }

    std::unique_ptr<ConditionNode> parseComparison() {
        const Token& fieldToken = expect(TokenType::Word, "Expected a field name in the WHERE clause.");
        const std::string fieldName = canonicalFieldName(fieldToken.text);

        if (fieldName.empty()) {
            throw std::runtime_error("Unknown field in WHERE clause: '" + fieldToken.text + "'");
        }

        const Token& opToken =
            expect(TokenType::ComparisonOperator, "Expected a comparison operator after field '" + fieldName + "'.");

        const Token& valueToken = peek();
        if (valueToken.type != TokenType::Number && valueToken.type != TokenType::Word &&
            valueToken.type != TokenType::StringLiteral) {
            throw std::runtime_error("Expected a comparison value after operator '" + opToken.text + "'.");
        }
        advance();

        if (isIntegerField(fieldName)) {
            if (valueToken.type != TokenType::Number) {
                throw std::runtime_error("Field '" + fieldName + "' expects a numeric value.");
            }
        } else {
            if (opToken.text != "=" && opToken.text != "!=") {
                throw std::runtime_error("Field '" + fieldName + "' only supports '=' and '!=' operators.");
            }
            if (valueToken.type == TokenType::Number) {
                throw std::runtime_error("Field '" + fieldName + "' expects a text value.");
            }
        }

        return std::make_unique<ComparisonNode>(fieldName, opToken.text, valueToken.text);
    }

    std::vector<std::string> parseSelectedColumns() {
        std::vector<std::string> columns;

        if (match(TokenType::Star)) {
            columns = {"bookId", "title", "author", "genre", "year", "pages", "rating"};
            return columns;
        }

        while (true) {
            const Token& columnToken =
                expect(TokenType::Word, "Expected a column name after SELECT or after a comma.");
            const std::string fieldName = canonicalFieldName(columnToken.text);
            if (fieldName.empty()) {
                throw std::runtime_error("Unknown selected column: '" + columnToken.text + "'");
            }

            columns.push_back(fieldName);

            if (!match(TokenType::Comma)) {
                break;
            }
        }

        return columns;
    }

    std::string parseTableName() {
        const Token& tableToken = expect(TokenType::Word, "Expected a table name after FROM.");
        if (toLowerCopy(tableToken.text) != "books") {
            throw std::runtime_error("Unknown table name: '" + tableToken.text + "'. Only 'books' is supported.");
        }
        return "books";
    }

    const Token& expect(TokenType expectedType, const std::string& message) {
        if (peek().type != expectedType) {
            throw std::runtime_error(message);
        }
        return advance();
    }

    void expectKeyword(const std::string& keyword) {
        const Token& token = peek();
        if (token.type != TokenType::Word || toUpperCopy(token.text) != keyword) {
            throw std::runtime_error("Expected keyword '" + keyword + "'.");
        }
        advance();
    }

    bool matchKeyword(const std::string& keyword) {
        const Token& token = peek();
        if (token.type == TokenType::Word && toUpperCopy(token.text) == keyword) {
            advance();
            return true;
        }
        return false;
    }

    bool match(TokenType type) {
        if (peek().type == type) {
            advance();
            return true;
        }
        return false;
    }

    const Token& peek() const {
        return tokens[position];
    }

    const Token& advance() {
        if (position < tokens.size()) {
            ++position;
        }
        return tokens[position - 1];
    }

    bool isAtEnd() const {
        return peek().type == TokenType::End;
    }

    std::vector<Token> tokens;
    std::size_t position = 0;
};

std::string valueForColumn(const Book& book, const std::string& column) {
    if (column == "bookId") {
        return std::to_string(book.bookId);
    }
    if (column == "title") {
        return book.title;
    }
    if (column == "author") {
        return book.author;
    }
    if (column == "genre") {
        return book.genre;
    }
    if (column == "year") {
        return std::to_string(book.year);
    }
    if (column == "pages") {
        return std::to_string(book.pages);
    }
    return std::to_string(book.rating);
}

void printDivider(const std::vector<std::size_t>& widths) {
    std::cout << '+';
    for (std::size_t width : widths) {
        std::cout << std::string(width + 2, '-') << '+';
    }
    std::cout << '\n';
}

void printResultTable(const std::vector<Book>& rows, const std::vector<std::string>& columns) {
    std::vector<std::size_t> widths;
    widths.reserve(columns.size());

    for (const std::string& column : columns) {
        widths.push_back(column.size());
    }

    for (const Book& book : rows) {
        for (std::size_t index = 0; index < columns.size(); ++index) {
            widths[index] = std::max(widths[index], valueForColumn(book, columns[index]).size());
        }
    }

    printDivider(widths);
    std::cout << '|';
    for (std::size_t index = 0; index < columns.size(); ++index) {
        std::cout << ' ' << std::left << std::setw(static_cast<int>(widths[index])) << columns[index] << " |";
    }
    std::cout << '\n';
    printDivider(widths);

    for (const Book& book : rows) {
        std::cout << '|';
        for (std::size_t index = 0; index < columns.size(); ++index) {
            std::cout << ' ' << std::left << std::setw(static_cast<int>(widths[index]))
                      << valueForColumn(book, columns[index]) << " |";
        }
        std::cout << '\n';
    }

    printDivider(widths);
}

std::string joinColumns(const std::vector<std::string>& columns) {
    std::ostringstream output;
    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (index != 0U) {
            output << ", ";
        }
        output << columns[index];
    }
    return output.str();
}

void runQueryDemo(const std::vector<Book>& books, const std::string& queryText) {
    std::cout << std::string(92, '=') << '\n';
    std::cout << "Query          : " << queryText << '\n';

    try {
        QueryParser parser(tokenizeQuery(queryText));
        ParsedQuery query = parser.parseQuery();

        std::vector<Book> resultRows;
        for (const Book& book : books) {
            if (!query.hasWhereClause || query.whereClause->evaluate(book)) {
                resultRows.push_back(book);
            }
        }

        std::cout << "Columns        : " << joinColumns(query.selectedColumns) << '\n';
        std::cout << "Table          : " << query.tableName << '\n';
        std::cout << "WHERE present  : " << (query.hasWhereClause ? "Yes" : "No") << '\n';
        std::cout << "Result count   : " << resultRows.size() << '\n';

        if (resultRows.empty()) {
            std::cout << "No rows matched this query.\n";
        } else {
            printResultTable(resultRows, query.selectedColumns);
        }
    } catch (const std::exception& error) {
        std::cout << "Parser error   : " << error.what() << '\n';
    }

    std::cout << '\n';
}

}  // namespace

int main() {
    const std::vector<Book> books = createBookTable();

    std::cout << "Lab 7 - SQL SELECT Query Parser\n";
    std::cout << "Dataset  : In-memory library catalogue\n\n";

    const std::vector<std::string> demoQueries = {
        "SELECT title, author FROM books WHERE rating >= 4 AND year > 2015",
        "SELECT * FROM books WHERE genre = Fiction OR pages < 250",
        "SELECT title, genre, rating FROM books WHERE ( genre = Mystery OR genre = Fiction ) AND rating >= 4",
        "SELECT title, year, rating FROM books",
        "SELECT title, price FROM books WHERE rating >= 4"
    };

    for (const std::string& query : demoQueries) {
        runQueryDemo(books, query);
    }

    return 0;
}
