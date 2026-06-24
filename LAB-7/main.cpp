#include <iostream>
#include <vector>
#include <string>
#include "types.h"
#include "lexer.h"
#include "parser.h"
#include "evaluator.h"

void shunting_demo() {
    std::string expr = "age * 2 + salary / 1000 > 100";
    Lexer lexer(expr);
    auto tokens = lexer.tokenize();
    auto rpn = SQLParser::toRPN(tokens);

    std::cout << "Expression : " << expr << "\n";
    std::cout << "RPN        : ";
    for (auto& t : rpn) {
        if (t.type == RpnTokenType::LITERAL) {
            if (t.val.type == Value::DOUBLE) {
                std::cout << t.val.d_val << " ";
            } else {
                std::cout << "'" << t.val.s_val << "' ";
            }
        } else {
            std::cout << t.val.s_val << " ";
        }
    }
    std::cout << "\n";

    Row row;
    row.cols["age"] = Value(30.0);
    row.cols["salary"] = Value(50000.0);
    bool result = Evaluator::evalRPN(rpn, row);
    std::cout << "Result     : " << (result ? "true" : "false") << "\n\n";
}

void print_rows(const std::vector<Row>& rows) {
    for (const auto& row : rows) {
        for (const auto& pair : row.cols) {
            const std::string& k = pair.first;
            const Value& v = pair.second;
            std::cout << k << "=";
            if (v.type == Value::DOUBLE) {
                std::cout << v.d_val;
            } else {
                std::cout << v.s_val;
            }
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}

int main() {
    std::cout << "=== Part 1: Dijkstra's Shunting-Yard Expression Evaluator Demo ===\n";
    shunting_demo();

    std::cout << "=== Part 2: Minimal SQL SELECT Parser over vector<Row> ===\n";
    // Pre-fetched data (simulates database records)
    std::vector<Row> students = {
        {{{ "id", Value(1.0) }, { "name", Value(std::string("Alice")) }, { "age", Value(22.0) }, { "gpa", Value(3.8) }, { "course", Value(std::string("Computer Science")) }}},
        {{{ "id", Value(2.0) }, { "name", Value(std::string("Bob"))   }, { "age", Value(25.0) }, { "gpa", Value(2.9) }, { "course", Value(std::string("Electronics")) }}},
        {{{ "id", Value(3.0) }, { "name", Value(std::string("Carol"))  }, { "age", Value(21.0) }, { "gpa", Value(3.5) }, { "course", Value(std::string("Computer Science")) }}},
        {{{ "id", Value(4.0) }, { "name", Value(std::string("Dave"))   }, { "age", Value(30.0) }, { "gpa", Value(3.1) }, { "course", Value(std::string("Mechanical")) }}},
    };

    // Test queries
    std::string queries[] = {
        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",
        "SELECT * FROM students WHERE age >= 22.0 AND age <= 26.0",
        "SELECT name, course FROM students WHERE course = 'Computer Science' OR age > 25.0",
        "SELECT * FROM students WHERE NOT (age >= 25.0)",
    };

    for (const auto& sql : queries) {
        std::cout << "SQL Query: " << sql << "\n";
        try {
            Lexer lexer(sql);
            auto tokens = lexer.tokenize();
            SQLParser parser(tokens);
            SelectQuery q = parser.parseSelect();
            
            auto res = Evaluator::execute(q, students);
            print_rows(res);
        } catch (const std::exception& ex) {
            std::cout << "Error executing query: " << ex.what() << "\n";
        }
        std::cout << "\n";
    }

    return 0;
}
