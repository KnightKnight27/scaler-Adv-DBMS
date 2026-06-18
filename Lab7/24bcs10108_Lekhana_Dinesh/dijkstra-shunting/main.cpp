#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stack>
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

struct EvalValue {
    bool isBoolean;
    std::string text;
    bool boolValue;

    static EvalValue makeText(const std::string& value) {
        return EvalValue{false, value, false};
    }

    static EvalValue makeBoolean(bool value) {
        return EvalValue{true, "", value};
    }
};

std::string toUpper(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool isKnownField(const std::string& token) {
    return token == "id" || token == "name" || token == "category" || token == "price" || token == "stock";
}

bool isNumericField(const std::string& field) {
    return field == "id" || field == "price" || field == "stock";
}

bool isComparisonOperator(const std::string& token) {
    return token == "=" || token == "!=" || token == "<" || token == "<=" || token == ">" || token == ">=";
}

bool isLogicalOperator(const std::string& token) {
    return token == "AND" || token == "OR";
}

bool isOperator(const std::string& token) {
    return isComparisonOperator(token) || isLogicalOperator(token);
}

int precedence(const std::string& token) {
    if (isComparisonOperator(token)) {
        return 3;
    }
    if (token == "AND") {
        return 2;
    }
    if (token == "OR") {
        return 1;
    }
    throw std::runtime_error("Invalid operator: " + token);
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

std::vector<Product> buildProducts() {
    return {
        {1, "WirelessMouse", "Electronics", 799, 25},
        {2, "DeskLamp", "HomeDecor", 450, 8},
        {3, "CoffeeMug", "Kitchen", 120, 0},
        {4, "OfficeChair", "Furniture", 3200, 7},
        {5, "YogaMat", "Fitness", 650, 18},
        {6, "SmartSpeaker", "Electronics", 1800, 12},
        {7, "NotebookSet", "Stationery", 80, 40},
        {8, "Blender", "Kitchen", 2200, 14},
        {9, "Bookshelf", "Furniture", 5100, 4}
    };
}

std::vector<std::string> tokenizeCondition(const std::string& condition) {
    std::vector<std::string> tokens;
    std::size_t index = 0;

    while (index < condition.size()) {
        const char current = condition[index];

        if (std::isspace(static_cast<unsigned char>(current))) {
            ++index;
            continue;
        }

        if (current == '(' || current == ')') {
            tokens.emplace_back(1, current);
            ++index;
            continue;
        }

        if (current == '<' || current == '>' || current == '=' || current == '!') {
            std::string token(1, current);

            if (index + 1 < condition.size() && condition[index + 1] == '=') {
                token.push_back('=');
                ++index;
            } else if (current == '!') {
                throw std::runtime_error("Invalid operator: !");
            }

            tokens.push_back(token);
            ++index;
            continue;
        }

        const std::size_t start = index;
        while (index < condition.size()) {
            const char ch = condition[index];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '(' || ch == ')' ||
                ch == '<' || ch == '>' || ch == '=' || ch == '!') {
                break;
            }
            ++index;
        }

        std::string token = condition.substr(start, index - start);
        const std::string upper = toUpper(token);
        if (upper == "AND" || upper == "OR") {
            tokens.push_back(upper);
        } else {
            tokens.push_back(token);
        }
    }

    if (tokens.empty()) {
        throw std::runtime_error("Condition is empty.");
    }

    return tokens;
}

std::vector<std::string> infixToPostfix(const std::vector<std::string>& tokens) {
    std::vector<std::string> postfix;
    std::stack<std::string> operators;

    for (const std::string& token : tokens) {
        if (token == "(") {
            operators.push(token);
        } else if (token == ")") {
            while (!operators.empty() && operators.top() != "(") {
                postfix.push_back(operators.top());
                operators.pop();
            }

            if (operators.empty()) {
                throw std::runtime_error("Mismatched parentheses in condition.");
            }

            operators.pop();
        } else if (isOperator(token)) {
            while (!operators.empty() && operators.top() != "(" &&
                   precedence(operators.top()) >= precedence(token)) {
                postfix.push_back(operators.top());
                operators.pop();
            }
            operators.push(token);
        } else {
            postfix.push_back(token);
        }
    }

    while (!operators.empty()) {
        if (operators.top() == "(") {
            throw std::runtime_error("Mismatched parentheses in condition.");
        }
        postfix.push_back(operators.top());
        operators.pop();
    }

    return postfix;
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
    throw std::runtime_error("Unknown field: " + field);
}

bool compareValues(const std::string& leftValue,
                   const std::string& op,
                   const std::string& rightValue,
                   bool numericField) {
    if (numericField) {
        const int leftNumber = parseInteger(leftValue);
        const int rightNumber = parseInteger(rightValue);

        if (op == "=") {
            return leftNumber == rightNumber;
        }
        if (op == "!=") {
            return leftNumber != rightNumber;
        }
        if (op == "<") {
            return leftNumber < rightNumber;
        }
        if (op == "<=") {
            return leftNumber <= rightNumber;
        }
        if (op == ">") {
            return leftNumber > rightNumber;
        }
        if (op == ">=") {
            return leftNumber >= rightNumber;
        }
        throw std::runtime_error("Invalid operator: " + op);
    }

    if (op == "=") {
        return leftValue == rightValue;
    }
    if (op == "!=") {
        return leftValue != rightValue;
    }

    throw std::runtime_error("Invalid operator for string comparison: " + op);
}

bool evaluatePostfix(const Product& product, const std::vector<std::string>& postfix) {
    std::stack<EvalValue> values;

    for (const std::string& token : postfix) {
        if (isComparisonOperator(token)) {
            if (values.size() < 2) {
                throw std::runtime_error("Malformed condition.");
            }

            const EvalValue rightSide = values.top();
            values.pop();
            const EvalValue leftSide = values.top();
            values.pop();

            if (leftSide.isBoolean || rightSide.isBoolean) {
                throw std::runtime_error("Malformed condition.");
            }
            if (!isKnownField(leftSide.text)) {
                throw std::runtime_error("Unknown field: " + leftSide.text);
            }

            const std::string fieldValue = getFieldValue(product, leftSide.text);
            const bool result = compareValues(fieldValue, token, rightSide.text, isNumericField(leftSide.text));
            values.push(EvalValue::makeBoolean(result));
        } else if (isLogicalOperator(token)) {
            if (values.size() < 2) {
                throw std::runtime_error("Malformed condition.");
            }

            const EvalValue rightSide = values.top();
            values.pop();
            const EvalValue leftSide = values.top();
            values.pop();

            if (!leftSide.isBoolean || !rightSide.isBoolean) {
                throw std::runtime_error("Malformed condition.");
            }

            const bool result = token == "AND"
                                    ? leftSide.boolValue && rightSide.boolValue
                                    : leftSide.boolValue || rightSide.boolValue;
            values.push(EvalValue::makeBoolean(result));
        } else {
            values.push(EvalValue::makeText(token));
        }
    }

    if (values.size() != 1 || !values.top().isBoolean) {
        throw std::runtime_error("Malformed condition.");
    }

    return values.top().boolValue;
}

void printProducts(const std::vector<Product>& products) {
    if (products.empty()) {
        std::cout << "No matching products found.\n";
        return;
    }

    std::cout << std::left
              << std::setw(5) << "ID"
              << std::setw(20) << "Name"
              << std::setw(15) << "Category"
              << std::setw(10) << "Price"
              << std::setw(8) << "Stock" << '\n';

    std::cout << std::string(58, '-') << '\n';

    for (const Product& product : products) {
        std::cout << std::left
                  << std::setw(5) << product.id
                  << std::setw(20) << product.name
                  << std::setw(15) << product.category
                  << std::setw(10) << product.price
                  << std::setw(8) << product.stock << '\n';
    }
}

void runConditionDemo(const std::vector<Product>& products, const std::string& condition) {
    std::cout << "\nCondition: " << condition << '\n';

    const std::vector<std::string> tokens = tokenizeCondition(condition);
    const std::vector<std::string> postfix = infixToPostfix(tokens);

    std::cout << "Tokens : " << formatTokens(tokens) << '\n';
    std::cout << "Postfix: " << formatTokens(postfix) << '\n';

    std::vector<Product> matches;
    for (const Product& product : products) {
        if (evaluatePostfix(product, postfix)) {
            matches.push_back(product);
        }
    }

    std::cout << "Matched rows:\n";
    printProducts(matches);
}

int main() {
    const std::vector<Product> products = buildProducts();
    const std::vector<std::string> conditions = {
        "price > 500 AND stock > 10",
        "category = Electronics OR price < 100",
        "( category = Electronics OR category = Furniture ) AND stock >= 5",
        "price <= 200 OR stock = 0"
    };

    std::cout << "Lab 7 - Dijkstra Shunting-Yard WHERE Evaluator\n";
    std::cout << "Product dataset size: " << products.size() << "\n";

    for (const std::string& condition : conditions) {
        try {
            runConditionDemo(products, condition);
        } catch (const std::runtime_error& error) {
            std::cout << "Error: " << error.what() << '\n';
        }
    }

    return 0;
}
