#include <algorithm>
#include <cctype>
#include <exception>
#include <iomanip>
#include <iostream>
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

enum class RawTokenType {
    Identifier,
    Number,
    StringLiteral,
    ComparisonOperator,
    LogicalOperator,
    LeftParen,
    RightParen
};

struct RawToken {
    RawTokenType type;
    std::string text;
    std::string display;
    std::size_t position;
};

struct Predicate {
    std::string field;
    std::string op;
    std::string value;
    std::string display;
    bool valueIsNumeric;
};

enum class ExpressionTokenType {
    Predicate,
    LogicalOperator,
    LeftParen,
    RightParen
};

struct ExpressionToken {
    ExpressionTokenType type;
    std::string text;
    Predicate predicate;
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

    const std::string lowered = toLowerCopy(tokenText);
    const auto it = fieldMap.find(lowered);
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

[[noreturn]] void throwParseError(std::size_t position, const std::string& message) {
    std::ostringstream builder;
    builder << "Token error near position " << position << ": " << message;
    throw std::runtime_error(builder.str());
}

std::vector<RawToken> tokenizeCondition(const std::string& condition) {
    std::vector<RawToken> tokens;

    for (std::size_t index = 0; index < condition.size();) {
        const unsigned char current = static_cast<unsigned char>(condition[index]);

        if (std::isspace(current)) {
            ++index;
            continue;
        }

        if (condition[index] == '(') {
            tokens.push_back({RawTokenType::LeftParen, "(", "(", index});
            ++index;
            continue;
        }

        if (condition[index] == ')') {
            tokens.push_back({RawTokenType::RightParen, ")", ")", index});
            ++index;
            continue;
        }

        if (condition[index] == '"' || condition[index] == '\'') {
            const char quote = condition[index];
            const std::size_t start = index;
            ++index;
            std::string value;

            while (index < condition.size() && condition[index] != quote) {
                value.push_back(condition[index]);
                ++index;
            }

            if (index == condition.size()) {
                throwParseError(start, "missing closing quote for string literal");
            }

            ++index;
            tokens.push_back({RawTokenType::StringLiteral, value, std::string(1, quote) + value + quote, start});
            continue;
        }

        if (condition[index] == '<' || condition[index] == '>' || condition[index] == '!' ||
            condition[index] == '=') {
            const std::size_t start = index;
            std::string op(1, condition[index]);
            ++index;

            if (index < condition.size() && condition[index] == '=') {
                op.push_back('=');
                ++index;
            }

            if (op == "!" || op == "<>" || op == "==") {
                throwParseError(start, "unsupported comparison operator '" + op + "'");
            }

            tokens.push_back({RawTokenType::ComparisonOperator, op, op, start});
            continue;
        }

        if (std::isdigit(current) ||
            ((condition[index] == '-' || condition[index] == '+') && index + 1 < condition.size() &&
             std::isdigit(static_cast<unsigned char>(condition[index + 1])))) {
            const std::size_t start = index;
            std::size_t end = index + 1;

            while (end < condition.size() && std::isdigit(static_cast<unsigned char>(condition[end]))) {
                ++end;
            }

            const std::string numberText = condition.substr(start, end - start);
            tokens.push_back({RawTokenType::Number, numberText, numberText, start});
            index = end;
            continue;
        }

        if (std::isalpha(current) || condition[index] == '_') {
            const std::size_t start = index;
            std::size_t end = index + 1;

            while (end < condition.size()) {
                const unsigned char next = static_cast<unsigned char>(condition[end]);
                if (!std::isalnum(next) && condition[end] != '_') {
                    break;
                }
                ++end;
            }

            const std::string word = condition.substr(start, end - start);
            const std::string upperWord = toUpperCopy(word);

            if (upperWord == "AND" || upperWord == "OR") {
                tokens.push_back({RawTokenType::LogicalOperator, upperWord, upperWord, start});
            } else {
                tokens.push_back({RawTokenType::Identifier, word, word, start});
            }

            index = end;
            continue;
        }

        throwParseError(index, std::string("unexpected character '") + condition[index] + "'");
    }

    if (tokens.empty()) {
        throw std::runtime_error("Condition is empty.");
    }

    return tokens;
}

ExpressionToken makePredicateToken(const std::string& field,
                                   const std::string& op,
                                   const std::string& value,
                                   const std::string& displayValue,
                                   bool valueIsNumeric) {
    const std::string predicateText = field + " " + op + " " + displayValue;
    return {ExpressionTokenType::Predicate, predicateText,
            Predicate{field, op, value, predicateText, valueIsNumeric}};
}

std::vector<ExpressionToken> buildExpressionTokens(const std::vector<RawToken>& rawTokens) {
    std::vector<ExpressionToken> expressionTokens;
    bool expectingPredicate = true;

    for (std::size_t index = 0; index < rawTokens.size();) {
        const RawToken& token = rawTokens[index];

        if (expectingPredicate) {
            if (token.type == RawTokenType::LeftParen) {
                expressionTokens.push_back({ExpressionTokenType::LeftParen, "(", {}});
                ++index;
                continue;
            }

            if (token.type != RawTokenType::Identifier) {
                throwParseError(token.position, "expected a field name or '('");
            }

            const std::string fieldName = canonicalFieldName(token.text);
            if (fieldName.empty()) {
                throwParseError(token.position, "unknown field '" + token.text + "'");
            }

            if (index + 2 >= rawTokens.size()) {
                throwParseError(token.position, "incomplete comparison after field '" + token.text + "'");
            }

            const RawToken& opToken = rawTokens[index + 1];
            const RawToken& valueToken = rawTokens[index + 2];

            if (opToken.type != RawTokenType::ComparisonOperator) {
                throwParseError(opToken.position, "expected comparison operator after field '" + fieldName + "'");
            }

            if (valueToken.type != RawTokenType::Number && valueToken.type != RawTokenType::Identifier &&
                valueToken.type != RawTokenType::StringLiteral) {
                throwParseError(valueToken.position, "expected a value after operator '" + opToken.text + "'");
            }

            if (isIntegerField(fieldName)) {
                if (valueToken.type != RawTokenType::Number) {
                    throwParseError(valueToken.position,
                                    "field '" + fieldName + "' needs a numeric value");
                }
            } else {
                if (opToken.text != "=" && opToken.text != "!=") {
                    throwParseError(opToken.position,
                                    "field '" + fieldName + "' only supports '=' and '!='");
                }

                if (valueToken.type == RawTokenType::Number) {
                    throwParseError(valueToken.position,
                                    "field '" + fieldName + "' needs a text value");
                }
            }

            expressionTokens.push_back(
                makePredicateToken(fieldName, opToken.text, valueToken.text, valueToken.display,
                                   valueToken.type == RawTokenType::Number));

            index += 3;
            expectingPredicate = false;
            continue;
        }

        if (token.type == RawTokenType::LogicalOperator) {
            expressionTokens.push_back({ExpressionTokenType::LogicalOperator, token.text, {}});
            ++index;
            expectingPredicate = true;
            continue;
        }

        if (token.type == RawTokenType::RightParen) {
            expressionTokens.push_back({ExpressionTokenType::RightParen, ")", {}});
            ++index;
            continue;
        }

        throwParseError(token.position, "expected a logical operator or ')'");
    }

    if (expectingPredicate) {
        throw std::runtime_error("Condition ends unexpectedly. A comparison is missing after the last operator.");
    }

    return expressionTokens;
}

int logicalPrecedence(const ExpressionToken& token) {
    if (token.type != ExpressionTokenType::LogicalOperator) {
        return -1;
    }

    if (token.text == "AND") {
        return 2;
    }

    if (token.text == "OR") {
        return 1;
    }

    return -1;
}

std::vector<ExpressionToken> convertToPostfix(const std::vector<ExpressionToken>& infixTokens) {
    std::vector<ExpressionToken> postfix;
    std::vector<ExpressionToken> stack;

    for (const ExpressionToken& token : infixTokens) {
        if (token.type == ExpressionTokenType::Predicate) {
            postfix.push_back(token);
            continue;
        }

        if (token.type == ExpressionTokenType::LogicalOperator) {
            while (!stack.empty() && stack.back().type == ExpressionTokenType::LogicalOperator &&
                   logicalPrecedence(stack.back()) >= logicalPrecedence(token)) {
                postfix.push_back(stack.back());
                stack.pop_back();
            }

            stack.push_back(token);
            continue;
        }

        if (token.type == ExpressionTokenType::LeftParen) {
            stack.push_back(token);
            continue;
        }

        if (token.type == ExpressionTokenType::RightParen) {
            bool matched = false;

            while (!stack.empty()) {
                if (stack.back().type == ExpressionTokenType::LeftParen) {
                    matched = true;
                    stack.pop_back();
                    break;
                }

                postfix.push_back(stack.back());
                stack.pop_back();
            }

            if (!matched) {
                throw std::runtime_error("Mismatched parentheses detected while converting to postfix form.");
            }
        }
    }

    while (!stack.empty()) {
        if (stack.back().type == ExpressionTokenType::LeftParen ||
            stack.back().type == ExpressionTokenType::RightParen) {
            throw std::runtime_error("Mismatched parentheses detected in condition.");
        }

        postfix.push_back(stack.back());
        stack.pop_back();
    }

    return postfix;
}

int readNumericField(const Book& book, const std::string& fieldName) {
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

std::string readTextField(const Book& book, const std::string& fieldName) {
    if (fieldName == "title") {
        return book.title;
    }
    if (fieldName == "author") {
        return book.author;
    }
    return book.genre;
}

bool evaluatePredicate(const Predicate& predicate, const Book& book) {
    if (isIntegerField(predicate.field)) {
        const int fieldValue = readNumericField(book, predicate.field);
        const int literal = std::stoi(predicate.value);

        if (predicate.op == "=") {
            return fieldValue == literal;
        }
        if (predicate.op == "!=") {
            return fieldValue != literal;
        }
        if (predicate.op == "<") {
            return fieldValue < literal;
        }
        if (predicate.op == "<=") {
            return fieldValue <= literal;
        }
        if (predicate.op == ">") {
            return fieldValue > literal;
        }
        return fieldValue >= literal;
    }

    const std::string fieldValue = readTextField(book, predicate.field);
    if (predicate.op == "=") {
        return fieldValue == predicate.value;
    }
    return fieldValue != predicate.value;
}

bool evaluatePostfix(const std::vector<ExpressionToken>& postfixTokens, const Book& book) {
    std::vector<bool> stack;

    for (const ExpressionToken& token : postfixTokens) {
        if (token.type == ExpressionTokenType::Predicate) {
            stack.push_back(evaluatePredicate(token.predicate, book));
            continue;
        }

        if (stack.size() < 2U) {
            throw std::runtime_error("Malformed postfix expression. Logical operator has too few operands.");
        }

        const bool right = stack.back();
        stack.pop_back();
        const bool left = stack.back();
        stack.pop_back();

        if (token.text == "AND") {
            stack.push_back(left && right);
        } else {
            stack.push_back(left || right);
        }
    }

    if (stack.size() != 1U) {
        throw std::runtime_error("Malformed expression. Evaluation did not end with a single boolean result.");
    }

    return stack.back();
}

std::string formatBookValue(const Book& book, const std::string& column) {
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

void printSeparator(const std::vector<std::size_t>& widths) {
    std::cout << '+';
    for (std::size_t width : widths) {
        std::cout << std::string(width + 2, '-') << '+';
    }
    std::cout << '\n';
}

void printBookTable(const std::vector<Book>& rows) {
    const std::vector<std::string> columns = {"bookId", "title", "author", "genre", "year", "pages", "rating"};
    std::vector<std::size_t> widths;
    widths.reserve(columns.size());

    for (const std::string& column : columns) {
        widths.push_back(column.size());
    }

    for (const Book& book : rows) {
        for (std::size_t index = 0; index < columns.size(); ++index) {
            widths[index] = std::max(widths[index], formatBookValue(book, columns[index]).size());
        }
    }

    printSeparator(widths);
    std::cout << '|';
    for (std::size_t index = 0; index < columns.size(); ++index) {
        std::cout << ' ' << std::left << std::setw(static_cast<int>(widths[index])) << columns[index] << " |";
    }
    std::cout << '\n';
    printSeparator(widths);

    for (const Book& book : rows) {
        std::cout << '|';
        for (std::size_t index = 0; index < columns.size(); ++index) {
            std::cout << ' ' << std::left << std::setw(static_cast<int>(widths[index]))
                      << formatBookValue(book, columns[index]) << " |";
        }
        std::cout << '\n';
    }

    printSeparator(widths);
}

std::string joinRawTokens(const std::vector<RawToken>& tokens) {
    std::ostringstream output;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (index != 0U) {
            output << ' ';
        }
        output << '[' << tokens[index].display << ']';
    }
    return output.str();
}

std::string joinExpressionTokens(const std::vector<ExpressionToken>& tokens) {
    std::ostringstream output;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (index != 0U) {
            output << ' ';
        }
        output << '[' << tokens[index].text << ']';
    }
    return output.str();
}

void runConditionDemo(const std::vector<Book>& books, const std::string& condition) {
    std::cout << std::string(84, '=') << '\n';
    std::cout << "Condition : " << condition << '\n';

    try {
        const std::vector<RawToken> rawTokens = tokenizeCondition(condition);
        const std::vector<ExpressionToken> infixTokens = buildExpressionTokens(rawTokens);
        const std::vector<ExpressionToken> postfixTokens = convertToPostfix(infixTokens);

        std::vector<Book> matches;
        for (const Book& book : books) {
            if (evaluatePostfix(postfixTokens, book)) {
                matches.push_back(book);
            }
        }

        std::cout << "Tokens    : " << joinRawTokens(rawTokens) << '\n';
        std::cout << "Postfix   : " << joinExpressionTokens(postfixTokens) << '\n';
        std::cout << "Matches   : " << matches.size() << '\n';

        if (matches.empty()) {
            std::cout << "No books satisfied this condition.\n";
        } else {
            printBookTable(matches);
        }
    } catch (const std::exception& error) {
        std::cout << "Error     : " << error.what() << '\n';
    }

    std::cout << '\n';
}

}  // namespace

int main() {
    const std::vector<Book> books = createBookTable();

    std::cout << "Lab 7 - Dijkstra Shunting-Yard WHERE Evaluator\n";
    std::cout << "Dataset  : In-memory library catalogue\n\n";

    const std::vector<std::string> demoConditions = {
        "rating >= 4 AND year > 2015",
        "genre = Fiction OR genre = Mystery",
        "( genre = Fiction OR genre = Mystery ) AND pages < 400",
        "rating > 5 AND year < 2000",
        "( genre = Fiction OR rating >= 4"
    };

    for (const std::string& condition : demoConditions) {
        runConditionDemo(books, condition);
    }

    return 0;
}
