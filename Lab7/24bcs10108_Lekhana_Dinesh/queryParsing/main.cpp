#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Product {
    int id;
    std::string name;
    std::string category;
    int price;
    int stock;
};

struct Condition {
    std::string field;
    std::string op;
    std::string value;
};

struct ParsedQuery {
    bool selectAll = false;
    std::vector<std::string> selectedColumns;
    std::string tableName;
    std::vector<Condition> conditions;
    std::vector<std::string> connectors;
};

std::string toUpper(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool isKnownColumn(const std::string& name) {
    return name == "id" || name == "name" || name == "category" || name == "price" || name == "stock";
}

bool isNumericColumn(const std::string& name) {
    return name == "id" || name == "price" || name == "stock";
}

bool isComparisonOperator(const std::string& token) {
    return token == "=" || token == "!=" || token == "<" || token == "<=" || token == ">" || token == ">=";
}

int parseInteger(const std::string& token) {
    std::size_t used = 0;
    int value = 0;

    try {
        value = std::stoi(token, &used);
    } catch (const std::exception&) {
        throw std::runtime_error("Expected an integer value, found: " + token);
    }

    if (used != token.size()) {
        throw std::runtime_error("Expected an integer value, found: " + token);
    }

    return value;
}

std::vector<Product> buildProducts() {
    return {
        {101, "GamingMouse", "Electronics", 1299, 15},
        {102, "FloorLamp", "HomeDecor", 950, 6},
        {103, "SnackBox", "Groceries", 90, 24},
        {104, "TravelBag", "Accessories", 1750, 9},
        {105, "MechanicalKeyboard", "Electronics", 3499, 11},
        {106, "WaterBottle", "Fitness", 250, 20},
        {107, "StudyTable", "Furniture", 5200, 4},
        {108, "Webcam", "Electronics", 2100, 14},
        {109, "StickyNotes", "Stationery", 60, 35}
    };
}

std::vector<std::string> allColumns() {
    return {"id", "name", "category", "price", "stock"};
}

std::string getFieldValue(const Product& product, const std::string& field) {
    if (field == "id") {
        return std::to_string(product.id);
    }
    if (field == "name") {
        return product.name;
    }
    if (field == "category") {
        return product.category;
    }
    if (field == "price") {
        return std::to_string(product.price);
    }
    if (field == "stock") {
        return std::to_string(product.stock);
    }
    throw std::runtime_error("Unknown selected column: " + field);
}

std::string formatTokens(const std::vector<std::string>& tokens) {
    std::ostringstream output;

    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (index > 0) {
            output << " ";
        }
        output << tokens[index];
    }

    return output.str();
}

std::string formatColumnList(const std::vector<std::string>& columns) {
    std::ostringstream output;

    for (std::size_t index = 0; index < columns.size(); ++index) {
        if (index > 0) {
            output << ", ";
        }
        output << columns[index];
    }

    return output.str();
}

std::vector<std::string> tokenizeQuery(const std::string& query) {
    std::vector<std::string> tokens;
    std::size_t index = 0;

    while (index < query.size()) {
        const char current = query[index];

        if (std::isspace(static_cast<unsigned char>(current))) {
            ++index;
            continue;
        }

        if (current == ',') {
            tokens.push_back(",");
            ++index;
            continue;
        }

        if (current == '<' || current == '>' || current == '=' || current == '!') {
            std::string token(1, current);

            if (index + 1 < query.size() && query[index + 1] == '=') {
                token.push_back('=');
                ++index;
            } else if (current == '!') {
                throw std::runtime_error("Invalid WHERE clause: bad operator '!'.");
            }

            tokens.push_back(token);
            ++index;
            continue;
        }

        const std::size_t start = index;
        while (index < query.size()) {
            const char ch = query[index];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == '<' ||
                ch == '>' || ch == '=' || ch == '!') {
                break;
            }
            ++index;
        }

        std::string token = query.substr(start, index - start);
        const std::string upper = toUpper(token);
        if (upper == "SELECT" || upper == "FROM" || upper == "WHERE" || upper == "AND" || upper == "OR") {
            tokens.push_back(upper);
        } else {
            tokens.push_back(token);
        }
    }

    return tokens;
}

Condition parseCondition(const std::vector<std::string>& tokens, std::size_t& index) {
    if (index + 2 >= tokens.size()) {
        throw std::runtime_error("Invalid WHERE clause.");
    }

    Condition condition{tokens[index], tokens[index + 1], tokens[index + 2]};

    if (!isKnownColumn(condition.field)) {
        throw std::runtime_error("Invalid WHERE clause: unknown field '" + condition.field + "'.");
    }
    if (!isComparisonOperator(condition.op)) {
        throw std::runtime_error("Invalid WHERE clause: bad operator '" + condition.op + "'.");
    }

    index += 3;
    return condition;
}

ParsedQuery parseQuery(const std::vector<std::string>& tokens) {
    if (tokens.empty() || tokens.front() != "SELECT") {
        throw std::runtime_error("Missing SELECT keyword.");
    }

    ParsedQuery parsed;
    std::size_t index = 1;

    while (index < tokens.size() && tokens[index] != "FROM") {
        const std::string& token = tokens[index];

        if (token == ",") {
            ++index;
            continue;
        }

        if (token == "*") {
            parsed.selectAll = true;
        } else {
            parsed.selectedColumns.push_back(token);
        }

        ++index;
    }

    if (index >= tokens.size() || tokens[index] != "FROM") {
        throw std::runtime_error("Missing FROM keyword.");
    }
    if (parsed.selectAll && !parsed.selectedColumns.empty()) {
        throw std::runtime_error("'*' cannot be combined with named columns.");
    }
    if (!parsed.selectAll && parsed.selectedColumns.empty()) {
        throw std::runtime_error("No columns were selected.");
    }

    ++index;
    if (index >= tokens.size()) {
        throw std::runtime_error("Invalid table name.");
    }

    parsed.tableName = tokens[index];
    if (toUpper(parsed.tableName) != "PRODUCTS") {
        throw std::runtime_error("Invalid table name. Only 'products' is allowed.");
    }

    ++index;
    if (index < tokens.size()) {
        if (tokens[index] != "WHERE") {
            throw std::runtime_error("Unexpected token after table name: " + tokens[index]);
        }

        ++index;
        parsed.conditions.push_back(parseCondition(tokens, index));

        while (index < tokens.size()) {
            const std::string connector = tokens[index];
            if (connector != "AND" && connector != "OR") {
                throw std::runtime_error("Invalid WHERE clause.");
            }

            parsed.connectors.push_back(connector);
            ++index;
            parsed.conditions.push_back(parseCondition(tokens, index));
        }
    }

    for (const std::string& column : parsed.selectedColumns) {
        if (!isKnownColumn(column)) {
            throw std::runtime_error("Unknown selected column: " + column);
        }
    }

    return parsed;
}

bool compareCondition(const Product& product, const Condition& condition) {
    const std::string fieldValue = getFieldValue(product, condition.field);

    if (isNumericColumn(condition.field)) {
        const int leftNumber = parseInteger(fieldValue);
        const int rightNumber = parseInteger(condition.value);

        if (condition.op == "=") {
            return leftNumber == rightNumber;
        }
        if (condition.op == "!=") {
            return leftNumber != rightNumber;
        }
        if (condition.op == "<") {
            return leftNumber < rightNumber;
        }
        if (condition.op == "<=") {
            return leftNumber <= rightNumber;
        }
        if (condition.op == ">") {
            return leftNumber > rightNumber;
        }
        if (condition.op == ">=") {
            return leftNumber >= rightNumber;
        }
    } else {
        if (condition.op == "=") {
            return fieldValue == condition.value;
        }
        if (condition.op == "!=") {
            return fieldValue != condition.value;
        }
        throw std::runtime_error("Invalid WHERE clause: string fields support only = and !=.");
    }

    throw std::runtime_error("Invalid WHERE clause.");
}

bool matchesWhereClause(const Product& product, const ParsedQuery& parsed) {
    if (parsed.conditions.empty()) {
        return true;
    }

    bool currentGroup = compareCondition(product, parsed.conditions[0]);
    bool hasOrGroup = false;
    bool orResult = false;

    for (std::size_t index = 0; index < parsed.connectors.size(); ++index) {
        const bool nextValue = compareCondition(product, parsed.conditions[index + 1]);

        if (parsed.connectors[index] == "AND") {
            currentGroup = currentGroup && nextValue;
        } else {
            orResult = hasOrGroup ? (orResult || currentGroup) : currentGroup;
            hasOrGroup = true;
            currentGroup = nextValue;
        }
    }

    return hasOrGroup ? (orResult || currentGroup) : currentGroup;
}

std::vector<std::string> outputColumns(const ParsedQuery& parsed) {
    return parsed.selectAll ? allColumns() : parsed.selectedColumns;
}

void printRows(const std::vector<Product>& products, const ParsedQuery& parsed) {
    const std::vector<std::string> columns = outputColumns(parsed);
    std::vector<Product> matches;

    for (const Product& product : products) {
        if (matchesWhereClause(product, parsed)) {
            matches.push_back(product);
        }
    }

    if (matches.empty()) {
        std::cout << "No matching rows found.\n";
        return;
    }

    std::vector<std::size_t> widths(columns.size(), 0);
    for (std::size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
        widths[columnIndex] = columns[columnIndex].size();
    }

    for (const Product& product : matches) {
        for (std::size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
            const std::string value = getFieldValue(product, columns[columnIndex]);
            widths[columnIndex] = std::max(widths[columnIndex], value.size());
        }
    }

    for (std::size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
        std::cout << std::left << std::setw(static_cast<int>(widths[columnIndex] + 3)) << columns[columnIndex];
    }
    std::cout << '\n';

    std::size_t separatorWidth = 0;
    for (std::size_t width : widths) {
        separatorWidth += width + 3;
    }
    std::cout << std::string(separatorWidth, '-') << '\n';

    for (const Product& product : matches) {
        for (std::size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
            std::cout << std::left
                      << std::setw(static_cast<int>(widths[columnIndex] + 3))
                      << getFieldValue(product, columns[columnIndex]);
        }
        std::cout << '\n';
    }
}

void runQueryDemo(const std::vector<Product>& products, const std::string& query) {
    std::cout << "\nQuery: " << query << '\n';

    const std::vector<std::string> tokens = tokenizeQuery(query);
    const ParsedQuery parsed = parseQuery(tokens);

    std::cout << "Tokens         : " << formatTokens(tokens) << '\n';
    std::cout << "Selected cols  : "
              << (parsed.selectAll ? "*" : formatColumnList(parsed.selectedColumns)) << '\n';
    std::cout << "Table          : " << parsed.tableName << '\n';
    std::cout << "WHERE present  : " << (parsed.conditions.empty() ? "No" : "Yes") << '\n';
    std::cout << "Result rows:\n";
    printRows(products, parsed);
}

int main() {
    const std::vector<Product> products = buildProducts();
    const std::vector<std::string> queries = {
        "SELECT name,price FROM products WHERE price > 500",
        "SELECT * FROM products WHERE category = Electronics AND stock > 5",
        "SELECT name,category,stock FROM products WHERE stock <= 10 OR price < 100",
        "SELECT id,name FROM products"
    };

    std::cout << "Lab 7 - Simple SQL Query Parser\n";
    std::cout << "Product dataset size: " << products.size() << "\n";

    for (const std::string& query : queries) {
        try {
            runQueryDemo(products, query);
        } catch (const std::runtime_error& error) {
            std::cout << "Error: " << error.what() << '\n';
        }
    }

    return 0;
}
