#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <sstream>
#include <unordered_map>

// Represents a row in our mock database
struct Row {
    std::unordered_map<std::string, int> columns;
};

// Evaluate operator precedence for Shunting-Yard
int precedence(char op) {
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    if (op == '<' || op == '>' || op == '=') return 0; // simplified logic
    return -1;
}

// Perform simple arithmetic/logical operation
int applyOp(int a, int b, char op) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return a / b;
        case '<': return a < b;
        case '>': return a > b;
        case '=': return a == b;
    }
    return 0;
}

// Evaluates a WHERE expression using Dijkstra's Shunting-Yard Algorithm
bool evaluateWhereClause(std::string tokens, Row& row) {
    std::stack<int> values;
    std::stack<char> ops;

    for (size_t i = 0; i < tokens.length(); i++) {
        if (tokens[i] == ' ') continue;

        if (isalpha(tokens[i])) { // It's a column name (simplified: 1 letter)
            std::string col(1, tokens[i]);
            values.push(row.columns[col]);
        } else if (isdigit(tokens[i])) { // It's a number
            int val = 0;
            while (i < tokens.length() && isdigit(tokens[i])) {
                val = (val * 10) + (tokens[i] - '0');
                i++;
            }
            values.push(val);
            i--;
        } else if (tokens[i] == '(') {
            ops.push(tokens[i]);
        } else if (tokens[i] == ')') {
            while (!ops.empty() && ops.top() != '(') {
                int val2 = values.top(); values.pop();
                int val1 = values.top(); values.pop();
                char op = ops.top(); ops.pop();
                values.push(applyOp(val1, val2, op));
            }
            if (!ops.empty()) ops.pop();
        } else { // It's an operator
            while (!ops.empty() && precedence(ops.top()) >= precedence(tokens[i])) {
                int val2 = values.top(); values.pop();
                int val1 = values.top(); values.pop();
                char op = ops.top(); ops.pop();
                values.push(applyOp(val1, val2, op));
            }
            ops.push(tokens[i]);
        }
    }

    while (!ops.empty()) {
        int val2 = values.top(); values.pop();
        int val1 = values.top(); values.pop();
        char op = ops.top(); ops.pop();
        values.push(applyOp(val1, val2, op));
    }

    return values.top() != 0; // Return true if the condition evaluates to > 0
}

// Minimal SQL SELECT parser
void executeSQL(const std::string& query, std::vector<Row>& table) {
    std::cout << "Executing: " << query << std::endl;
    
    // Very naive parser for "SELECT * FROM t WHERE <expr>"
    size_t where_pos = query.find("WHERE");
    if (where_pos == std::string::npos) {
        std::cout << "Error: Only supports WHERE queries." << std::endl;
        return;
    }

    std::string condition = query.substr(where_pos + 6);
    std::cout << "Extracted Condition: " << condition << std::endl;

    std::cout << "Results:" << std::endl;
    for (auto& row : table) {
        if (evaluateWhereClause(condition, row)) {
            std::cout << "Match -> ";
            for (auto const& [key, val] : row.columns) {
                std::cout << key << ":" << val << " ";
            }
            std::cout << std::endl;
        }
    }
}

int main() {
    // Populate mock table
    std::vector<Row> table = {
        {{{"a", 5}, {"b", 10}}},
        {{{"a", 15}, {"b", 5}}},
        {{{"a", 2}, {"b", 2}}}
    };

    std::cout << "--- Lab 5: Shunting-Yard + SQL Parser ---" << std::endl;
    // Condition: a + b > 10 (Row 1: 15 > 10, Row 2: 20 > 10, Row 3: 4 > 10)
    executeSQL("SELECT * FROM table WHERE a + b > 10", table);

    return 0;
}
